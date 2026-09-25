#include "tv/stripes.h"

#include <algorithm>
#include <cmath>

#include "tv/frame.h"

namespace tv {
namespace {

constexpr int W = kFrameWidth, H = kImageRows;
static_assert(W % 8 == 0, "the row sums run 8 columns at a time");

int reflect101(int k, int n) {
  if (n == 1) return 0;
  while (k < 0 || k >= n) k = k < 0 ? -k : 2 * (n - 1) - k;
  return k;
}

// The cubic convolution kernel's 4 weights for sampling at fraction t in [0, 1) past tap 0 (taps
// -1, 0, 1, 2), a = -0.75 as OpenCV's.
std::array<float, 4> cubicWeights(double t) {
  constexpr double a = -0.75;
  auto k = [](double x) {
    x = std::fabs(x);
    if (x <= 1.0) return ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0;
    if (x < 2.0) return ((a * x - 5.0 * a) * x + 8.0 * a) * x - 4.0 * a;
    return 0.0;
  };
  return {float(k(1.0 + t)), float(k(t)), float(k(1.0 - t)), float(k(2.0 - t))};
}


// Half size, blurred with [1 4 6 4 1] / 16 first (as OpenCV's pyrDown): (w + 1) / 2 x (h + 1) / 2.
// Down the columns first, whole rows at a time (vectorizes), then along the half rows.
void pyrDown(const float* in, int w, int h, std::vector<float>& out, std::vector<float>& scratch) {
  const int w2 = (w + 1) / 2, h2 = (h + 1) / 2;
  scratch.resize(size_t(w) * h2 + size_t(w) + 4);
  out.resize(size_t(w2) * h2);
  for (int y = 0; y < h2; ++y) {
    const float* r0 = in + size_t(reflect101(2 * y - 2, h)) * w;
    const float* r1 = in + size_t(reflect101(2 * y - 1, h)) * w;
    const float* r2 = in + size_t(reflect101(2 * y, h)) * w;
    const float* r3 = in + size_t(reflect101(2 * y + 1, h)) * w;
    const float* r4 = in + size_t(reflect101(2 * y + 2, h)) * w;
    float* v = scratch.data() + size_t(y) * w;
    for (int x = 0; x < w; ++x) v[x] = (r0[x] + r4[x] + 4.0f * (r1[x] + r3[x]) + 6.0f * r2[x]) * (1.0f / 16.0f);
  }
  for (int y = 0; y < h2; ++y) {
    const float* v = scratch.data() + size_t(y) * w;
    float* o = out.data() + size_t(y) * w2;
    for (int x = 0; x < w2; ++x) {
      const int c = 2 * x;
      if (c >= 2 && c + 2 < w) {
        o[x] = (v[c - 2] + v[c + 2] + 4.0f * (v[c - 1] + v[c + 1]) + 6.0f * v[c]) * (1.0f / 16.0f);
      } else {
        o[x] = (v[reflect101(c - 2, w)] + v[reflect101(c + 2, w)] + 4.0f * (v[reflect101(c - 1, w)] + v[reflect101(c + 1, w)]) +
                6.0f * v[c]) * (1.0f / 16.0f);
      }
    }
  }
}

// A w x h image less its column and row means (plus its mean), in place.
void unstripeLevel(float* img, int w, int h) {
  std::vector<double> col(size_t(w), 0.0), row(size_t(h), 0.0);
  double all = 0.0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const double v = img[size_t(y) * w + x];
      col[size_t(x)] += v;
      row[size_t(y)] += v;
      all += v;
    }
  all /= double(size_t(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      img[size_t(y) * w + x] = float(img[size_t(y) * w + x] - col[size_t(x)] / h - row[size_t(y)] / w + all);
}


// Central differences (one-sided at the edges), as numpy's gradient.
void gradients(const float* a, int w, int h, float* gx, float* gy) {
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = size_t(y) * w + x;
      gx[i] = x == 0 ? a[i + 1] - a[i] : x == w - 1 ? a[i] - a[i - 1] : 0.5f * (a[i + 1] - a[i - 1]);
      gy[i] = y == 0 ? a[i + size_t(w)] - a[i] : y == h - 1 ? a[i] - a[i - size_t(w)]
                                                           : 0.5f * (a[i + size_t(w)] - a[i - size_t(w)]);
    }
}

float medianOf(std::vector<float>& v) {
  if (v.empty()) return 0.0f;
  auto mid = v.begin() + ptrdiff_t(v.size() / 2);
  std::nth_element(v.begin(), mid, v.end());
  return *mid;
}

// One pyramid level of Lucas-Kanade for a global translation (b(p) = a(p - d)), Tukey-weighted,
// inverse compositional: a's gradients once, then per iteration b brought back by d (one warp),
// r = b(p + d) - a(p), and the step that the gradients of a say closes it.
std::array<double, 2> lkLevel(const float* a, const float* b, int w, int h, std::array<double, 2> d, int iters,
                              double noiseFloor, std::vector<float>& scratch, std::vector<float>& gx,
                              std::vector<float>& gy, std::vector<float>& bw, std::vector<float>& sample,
                              double significance, double* stderror = nullptr) {
  const size_t n = size_t(w) * h;
  gx.resize(n);
  gy.resize(n);
  bw.resize(n);
  gradients(a, w, h, gx.data(), gy.data());
  constexpr int m = 4;
  constexpr int kPlain = 2;  // plain least squares first: while still misaligned, the edges' large residuals
                             // carry the motion, and a robust weight would throw them out and stall
  double inv = 0.0;
  for (int it = 0; it < iters; ++it) {
    shiftImage(b, bw.data(), w, h, -d[0], -d[1], scratch);  // bw(p) = b(p + d)
    if (it == kPlain) {  // then Tukey weights, the scale (MAD) from a sparse subsample, floored at the noise
      sample.clear();
      for (int y = m; y < h - m; y += 3)
        for (int x = m + (y % 4); x < w - m; x += 5) sample.push_back(bw[size_t(y) * w + x] - a[size_t(y) * w + x]);
      const float med = medianOf(sample);
      for (float& v : sample) v = std::fabs(v - med);
      inv = 1.0 / (4.685 * std::max(1.4826 * medianOf(sample), noiseFloor) + 1e-6);
    }
    double axx = 0, axy = 0, ayy = 0, bx = 0, by = 0, rr = 0, ww = 0;
    for (int y = m; y < h - m; ++y) {
      const float* br = bw.data() + size_t(y) * w;
      const float* ar = a + size_t(y) * w;
      const float* gxr = gx.data() + size_t(y) * w;
      const float* gyr = gy.data() + size_t(y) * w;
      for (int x = m; x < w - m; ++x) {
        const double r = br[x] - ar[x];
        double wt = 1.0;
        if (it >= kPlain) {
          const double u = r * inv;
          if (u * u >= 1.0) continue;
          wt = (1.0 - u * u) * (1.0 - u * u);
        }
        const double ix = gxr[x], iy = gyr[x];
        rr += wt * r * r;
        ww += wt;
        axx += wt * ix * ix;
        axy += wt * ix * iy;
        ayy += wt * iy * iy;
        bx += wt * ix * r;
        by += wt * iy * r;
      }
    }
    const double det = axx * ayy - axy * axy, tr = axx + ayy;
    if (!(det > 1e-12 * tr * tr) || !(tr > 0)) {  // (too little structure to tell)
      if (stderror) *stderror = 1e9;
      break;
    }
    if (stderror) {  // the estimate's standard error: residual variance x (H^-1), the larger axis
      const double var = ww > 0 ? rr / ww : 0.0;
      *stderror = std::sqrt(var * std::max(ayy, axx) / det);
    }
    // The first step is a score test: with no motion, its squared Mahalanobis length (b^T H^-1 b over
    // the residuals' variance) is chi-square with 2 degrees of freedom. Unless it's significant, the
    // level stays where it started. Iterating on noise walks to a random peak of the two images'
    // correlation, half a pixel or more away at this level: on a scene with no structure left after the
    // unstriping, jumps of several pixels a frame (PIPELINE_LOG).
    if (it == 0 && significance > 0.0) {
      const double var = ww > 0 ? rr / ww : 0.0;
      const double m2 = (ayy * bx * bx - 2.0 * axy * bx * by + axx * by * by) / det;
      if (!(m2 > significance * significance * var)) break;
    }
    // r + grad a . delta = 0: delta = -(sum w g g^T)^-1 sum w g r
    const double dx = -(ayy * bx - axy * by) / det, dy = -(axx * by - axy * bx) / det;
    d[0] += dx;
    d[1] += dy;
    if (it >= kPlain && std::max(std::fabs(dx), std::fabs(dy)) < 0.005) break;
  }
  return d;
}

// A profile less its smooth part (a Gaussian of sigma, reflected ends).
void highPass(std::vector<float>& p, double sigma) {
  if (sigma <= 0.0) return;
  const int n = int(p.size()), r = std::max(1, int(std::ceil(3.0 * sigma)));
  std::vector<double> k(size_t(2 * r + 1));
  double sum = 0.0;
  for (int i = -r; i <= r; ++i) sum += (k[size_t(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma)));
  std::vector<float> smooth(p.size());
  for (int x = 0; x < n; ++x) {
    double s = 0.0;
    for (int i = -r; i <= r; ++i) s += k[size_t(i + r)] * p[size_t(reflect101(x + i, n))];
    smooth[size_t(x)] = float(s / sum);
  }
  for (int x = 0; x < n; ++x) p[size_t(x)] -= smooth[size_t(x)];
}

// An integer map shifted by (dx, dy) rounded to whole pixels, out(p) = in(p - d), edges reflected.
void shiftNearest(const int* in, int* out, double dx, double dy) {
  const int ix = int(std::lround(dx)), iy = int(std::lround(dy));
  const int lo = std::clamp(ix, 0, W), hi = std::clamp(W + ix, lo, W);  // (x - ix inside the row)
  for (int y = 0; y < H; ++y) {
    const int* row = in + size_t(reflect101(y - iy, H)) * W;
    int* o = out + size_t(y) * W;
    for (int x = 0; x < lo; ++x) o[x] = row[reflect101(x - ix, W)];
    std::copy(row + lo - ix, row + hi - ix, o + lo);
    for (int x = hi; x < W; ++x) o[x] = row[reflect101(x - ix, W)];
  }
}

}  // namespace

void shiftImage(const float* in, float* out, int w, int h, double dx, double dy, std::vector<float>& scratch) {
  if (dx == 0.0 && dy == 0.0) {
    std::copy(in, in + size_t(w) * h, out);
    return;
  }
  // out(p) = in(p - d): sample at x - dx = x + sx, sx = -dx = ix + tx (tx in [0, 1)).
  const double sx = -dx, sy = -dy;
  const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
  const std::array<float, 4> wx = cubicWeights(sx - ix), wy = cubicWeights(sy - iy);
  scratch.resize(size_t(w) * h);
  // (taps x + ix - 1 .. x + ix + 2: inside the row for x in [lo, hi), reflected only outside it)
  const int lo = std::clamp(1 - ix, 0, w), hi = std::clamp(w - 2 - ix, lo, w);
  for (int y = 0; y < h; ++y) {
    const float* row = in + size_t(y) * w;
    float* o = scratch.data() + size_t(y) * w;
    const auto edge = [&](int x) {
      const int x0 = x + ix;
      o[x] = wx[0] * row[reflect101(x0 - 1, w)] + wx[1] * row[reflect101(x0, w)] + wx[2] * row[reflect101(x0 + 1, w)] +
             wx[3] * row[reflect101(x0 + 2, w)];
    };
    for (int x = 0; x < lo; ++x) edge(x);
    const float* p = row + ix - 1;
    for (int x = lo; x < hi; ++x) o[x] = wx[0] * p[x] + wx[1] * p[x + 1] + wx[2] * p[x + 2] + wx[3] * p[x + 3];
    for (int x = hi; x < w; ++x) edge(x);
  }
  for (int y = 0; y < h; ++y) {
    const int y0 = y + iy;
    const float* r0 = scratch.data() + size_t(reflect101(y0 - 1, h)) * w;
    const float* r1 = scratch.data() + size_t(reflect101(y0, h)) * w;
    const float* r2 = scratch.data() + size_t(reflect101(y0 + 1, h)) * w;
    const float* r3 = scratch.data() + size_t(reflect101(y0 + 2, h)) * w;
    float* o = out + size_t(y) * w;
    for (int x = 0; x < w; ++x) o[x] = wy[0] * r0[x] + wy[1] * r1[x] + wy[2] * r2[x] + wy[3] * r3[x];
  }
}

std::array<double, 2> estimateShift(const float* a, const float* b, std::vector<float>& scratch) {
  return estimateShift(a, b, scratch, {0.0, 0.0}, 1.0);
}

std::array<double, 2> estimateShift(const float* a, const float* b, std::vector<float>& scratch,
                                    std::array<double, 2> guess, double noise, double* stderror,
                                    double significance) {
  ShiftScratch s;
  s.tmp.swap(scratch);
  const auto d = estimateShift(a, b, s, guess, noise, stderror, significance);
  s.tmp.swap(scratch);
  return d;
}

std::array<double, 2> estimateShift(const float* a, const float* b, ShiftScratch& s, std::array<double, 2> guess,
                                    double noise, double* stderror, double significance) {
  // At half resolution and below, each level less its column and row means so stripes can't read as
  // motion. Accurate to ~0.07 px at worst (Lucas-Kanade's ~0.035 px at its finest level, half the
  // frame's; measured with exact Fourier shifts): through a pan the stripe fix does as well with it as
  // with the true motion, and a full-resolution level would cost ~1 ms more on the tablet.
  constexpr int kLevels = ShiftScratch::kLevels;  // 128 x 96, 64 x 48, 32 x 24
  auto& pa = s.pa;
  auto& pb = s.pb;
  auto& scratch = s.tmp;
  std::array<int, kLevels> ws{}, hs{};
  pyrDown(a, W, H, pa[0], scratch);
  pyrDown(b, W, H, pb[0], scratch);
  ws[0] = (W + 1) / 2;
  hs[0] = (H + 1) / 2;
  for (int l = 1; l < kLevels; ++l) {
    pyrDown(pa[size_t(l - 1)].data(), ws[size_t(l - 1)], hs[size_t(l - 1)], pa[size_t(l)], scratch);
    pyrDown(pb[size_t(l - 1)].data(), ws[size_t(l - 1)], hs[size_t(l - 1)], pb[size_t(l)], scratch);
    ws[size_t(l)] = (ws[size_t(l - 1)] + 1) / 2;
    hs[size_t(l)] = (hs[size_t(l - 1)] + 1) / 2;
  }
  for (int l = 0; l < kLevels; ++l) {
    unstripeLevel(pa[size_t(l)].data(), ws[size_t(l)], hs[size_t(l)]);
    unstripeLevel(pb[size_t(l)].data(), ws[size_t(l)], hs[size_t(l)]);
  }
  auto& gx = s.gx;
  auto& gy = s.gy;
  auto& bw = s.bw;
  auto& sample = s.sample;
  std::array<double, 2> d{guess[0] / double(2 << (kLevels - 1)), guess[1] / double(2 << (kLevels - 1))};
  for (int l = kLevels - 1; l >= 0; --l) {
    // (each level's noise: the pyramid's blur takes it down ~2x a level; residuals carry two images')
    const double floor = noise * 1.4142 / double(2 << l);
    double err = 0.0;
    d = lkLevel(pa[size_t(l)].data(), pb[size_t(l)].data(), ws[size_t(l)], hs[size_t(l)], d, l == 0 ? 6 : 5, floor,
                scratch, gx, gy, bw, sample, significance, &err);
    if (l == 0 && stderror) *stderror = 2.0 * err;  // (level 0 is half the frame)
    d[0] *= 2.0;  // (each level is half the one below; level 0 is half the frame)
    d[1] *= 2.0;
  }
  return d;
}

void FrameStripes::reset() {
  started_ = false;
  updatePending_ = false;
  cum_[0] = cum_[1] = 0.0;
  moved_[0] = moved_[1] = 0.0;
  lastShift_ = {0.0f, 0.0f};
  lastMatched_ = 0.0f;
  lastCorrected_ = false;
}

void FrameStripes::process(float* sig, float sigma) { process(sig, sig, sigma); }

void FrameStripes::process(float* sig, const float* source, float sigma) {
  const StripeOptions& o = options_;
  ref_.resize(kImagePixels);
  refSensor_.resize(kImagePixels);
  diff_.resize(kImagePixels);
  scene_.resize(kImagePixels);
  age_.resize(kImagePixels);
  ageSensor_.resize(kImagePixels);
  matched_.resize(kImagePixels);
  lastCorrected_ = false;
  lastShift_ = {0.0f, 0.0f};
  updatePending_ = false;
  uncorrected_.assign(source, source + kImagePixels);  // (the reference follows it: see updateReference)
  if (!started_) {  // the first frame (or after a restart): the reference begins here
    std::copy(source, source + kImagePixels, ref_.begin());
    std::fill(age_.begin(), age_.end(), 0);
    cum_[0] = cum_[1] = moved_[0] = moved_[1] = 0.0;
    started_ = true;
    lastMatched_ = 0.0f;
    return;
  }
  // Where the scene went on the sensor since the reference last saw it.
  shiftImage(ref_.data(), refSensor_.data(), W, H, cum_[0], cum_[1], tmp_);
  double stderror = 0.0;
  const std::array<double, 2> delta =
      estimateShift(refSensor_.data(), source, shift_, {0.0, 0.0}, sigma, &stderror, o.significance);
  lastStdError_ = float(stderror);
  // The motion since the reference began: followed, the sum of the residual shifts (cum); not
  // followed, the reference stayed put, so it's this frame's own shift.
  if (o.compensate) {
    if (std::max(std::fabs(delta[0]), std::fabs(delta[1])) > o.minShift) {
      moved_[0] += delta[0];
      moved_[1] += delta[1];
    }
  } else {
    moved_[0] = delta[0];
    moved_[1] = delta[1];
  }
  if (std::hypot(moved_[0], moved_[1]) > o.maxMotion) {  // too far from where the reference began
    std::copy(source, source + kImagePixels, ref_.begin());
    std::fill(age_.begin(), age_.end(), 0);
    cum_[0] = cum_[1] = moved_[0] = moved_[1] = 0.0;
    lastShift_ = {float(delta[0]), float(delta[1])};
    lastMatched_ = 0.0f;
    return;
  }
  if (o.compensate && std::max(std::fabs(delta[0]), std::fabs(delta[1])) > o.minShift) {
    cum_[0] += delta[0];
    cum_[1] += delta[1];
    lastShift_ = {float(delta[0]), float(delta[1])};
    if (std::max(std::fabs(cum_[0]), std::fabs(cum_[1])) > o.anchor) {  // re-anchor: one extra interpolation
      shiftImage(ref_.data(), scene_.data(), W, H, cum_[0], cum_[1], tmp_);
      std::copy(scene_.begin(), scene_.end(), ref_.begin());
      shiftNearest(age_.data(), ageSensor_.data(), cum_[0], cum_[1]);
      std::copy(ageSensor_.begin(), ageSensor_.end(), age_.begin());
      cum_[0] = cum_[1] = 0.0;
    }
    shiftImage(ref_.data(), refSensor_.data(), W, H, cum_[0], cum_[1], tmp_);
  }
  shiftNearest(age_.data(), ageSensor_.data(), cum_[0], cum_[1]);
  const float* r = refSensor_.data();
  values_.clear();
  for (size_t i = 5; i < kImagePixels; i += 23) values_.push_back(source[i] - r[i]);  // (a subsample: every column and row)
  const float med = medianOf(values_);
  const float g = o.gate * sigma * 1.4f;
  const float edge2 = 2.0f * (o.edge * sigma);
  // One pass: frame - reference less the frame's shared offset (it's not a stripe), the gate,
  // freshness, and (a misalignment of a few hundredths of a pixel during a pan would leave a residual
  // along a strong vertical or horizontal edge, under the gate, that its column or row would take for
  // an offset) the edges; then each column's first mean over the pixels that pass.
  // (float sums: at most 192 values under the gate each, so the rounding stays ~1e-6 counts, and the
  // passes vectorize across the columns)
  std::vector<float>& colSum = colSum_;
  std::vector<int>& colCount = colCount_;
  colSum.assign(W, 0.0f);
  colCount.assign(W, 0);
  size_t unchanged = 0;
  for (int y = 0; y < H; ++y) {
    const float* sr = source + size_t(y) * W;
    float* d = diff_.data() + size_t(y) * W;
    const float* rr = r + size_t(y) * W;
    const float* up = r + size_t(y > 0 ? y - 1 : y) * W;
    const float* dn = r + size_t(y < H - 1 ? y + 1 : y) * W;
    const int* ag = ageSensor_.data() + size_t(y) * W;
    unsigned char* mk = matched_.data() + size_t(y) * W;
    const auto cell = [&](int x, int xl, int xr) {
      float v = sr[x] - rr[x];
      v -= med;
      d[x] = v;
      const bool still = std::fabs(v) < g;
      unchanged += still;
      const bool flat = std::fabs(rr[xr] - rr[xl]) < edge2 && std::fabs(dn[x] - up[x]) < edge2;
      const bool vote = still && flat && ag[x] >= o.fresh;
      mk[x] = vote;
      colSum[size_t(x)] += vote ? v : 0.0f;
      colCount[size_t(x)] += vote;
    };
    cell(0, 0, 1);
    for (int x = 1; x < W - 1; ++x) cell(x, x - 1, x + 1);
    cell(W - 1, W - 2, W - 1);
  }
  lastMatched_ = float(unchanged) / float(kImagePixels);
  if (lastMatched_ < o.lost) {  // can't follow the scene: start over from this frame
    std::copy(source, source + kImagePixels, ref_.begin());
    std::fill(age_.begin(), age_.end(), 0);
    cum_[0] = cum_[1] = moved_[0] = moved_[1] = 0.0;
    return;
  }
  // Each offset: its first mean, then again over the values within band x 1.4 sigma of it (a one-step
  // M-estimate: as robust as a trimmed mean here, the gate having bounded every value, and cheaper).
  // Columns first; the rows from what the columns leave.
  colOff_.assign(W, 0.0f);
  rowOff_.assign(H, 0.0f);
  const float band = o.band * 1.4f * sigma;
  std::vector<float>& first = first_;
  first.resize(W);
  for (int x = 0; x < W; ++x) first[size_t(x)] = colCount[size_t(x)] ? colSum[size_t(x)] / float(colCount[size_t(x)]) : 0.0f;
  std::fill(colSum.begin(), colSum.end(), 0.0f);
  std::fill(colCount.begin(), colCount.end(), 0);
  for (int y = 0; y < H; ++y) {
    const float* d = diff_.data() + size_t(y) * W;
    const unsigned char* mk = matched_.data() + size_t(y) * W;
    for (int x = 0; x < W; ++x) {
      const bool keep = mk[x] && std::fabs(d[x] - first[size_t(x)]) < band;
      colSum[size_t(x)] += keep ? d[x] : 0.0f;
      colCount[size_t(x)] += keep;
    }
  }
  for (int x = 0; x < W; ++x)
    colOff_[size_t(x)] = colCount[size_t(x)] >= H / 2 ? colSum[size_t(x)] / float(colCount[size_t(x)]) : 0.0f;
  for (int y = 0; y < H; ++y) {
    const float* d = diff_.data() + size_t(y) * W;
    const unsigned char* mk = matched_.data() + size_t(y) * W;
    // (8 interleaved partial sums, so the additions don't wait on each other; W is a multiple of 8)
    float a1[8] = {}, a2[8] = {};
    int c1[8] = {}, c2[8] = {};
    const float* co = colOff_.data();
    for (int x = 0; x < W; x += 8)
      for (int k = 0; k < 8; ++k) {
        a1[k] += mk[x + k] ? d[x + k] - co[x + k] : 0.0f;
        c1[k] += mk[x + k];
      }
    const float s1 = ((a1[0] + a1[1]) + (a1[2] + a1[3])) + ((a1[4] + a1[5]) + (a1[6] + a1[7]));
    const int n1 = c1[0] + c1[1] + c1[2] + c1[3] + c1[4] + c1[5] + c1[6] + c1[7];
    const float f1 = n1 ? s1 / float(n1) : 0.0f;
    for (int x = 0; x < W; x += 8)
      for (int k = 0; k < 8; ++k) {
        const float v = d[x + k] - co[x + k];
        const bool keep = mk[x + k] && std::fabs(v - f1) < band;
        a2[k] += keep ? v : 0.0f;
        c2[k] += keep;
      }
    const float s2 = ((a2[0] + a2[1]) + (a2[2] + a2[3])) + ((a2[4] + a2[5]) + (a2[6] + a2[7]));
    const int n2 = c2[0] + c2[1] + c2[2] + c2[3] + c2[4] + c2[5] + c2[6] + c2[7];
    rowOff_[size_t(y)] = n2 >= W / 2 ? s2 / float(n2) : 0.0f;
  }
  auto zeroMean = [](std::vector<float>& v) {
    double s = 0.0;
    for (float f : v) s += f;
    const float mean = float(s / double(v.size()));
    for (float& f : v) f -= mean;
  };
  zeroMean(colOff_);
  zeroMean(rowOff_);
  highPass(colOff_, o.highpass / 2.0);
  highPass(rowOff_, o.highpass / 2.0);
  gateUsed_ = g;
  updatePending_ = true;
  for (int y = 0; y < H; ++y) {
    float* sg = sig + size_t(y) * W;
    const float ro = rowOff_[size_t(y)];
    for (int x = 0; x < W; ++x) sg[x] -= std::clamp(colOff_[size_t(x)] + ro, -o.clamp, o.clamp);
  }
  lastCorrected_ = true;
}

void FrameStripes::updateReference() {
  if (!updatePending_) return;
  updatePending_ = false;
  // The reference follows the scene: the frame as it came (the corrected one would lock in whatever
  // pattern the reference started with), moved into scene coordinates. Where it was unchanged (under
  // the gate: whether or not it voted), the reference moves toward it; where it changed, the
  // reference restarts from it.
  shiftImage(uncorrected_.data(), scene_.data(), W, H, -cum_[0], -cum_[1], tmp_);
  flags_.resize(kImagePixels);
  flagsScene_.resize(kImagePixels);
  for (size_t i = 0; i < kImagePixels; ++i) flags_[i] = std::fabs(diff_[i]) < gateUsed_ ? 1 : 2;
  shiftNearest(flags_.data(), flagsScene_.data(), -cum_[0], -cum_[1]);
  const float alpha = 1.0f / std::max(options_.tauFrames, 1.0f);
  for (size_t i = 0; i < kImagePixels; ++i) {
    const bool keep = flagsScene_[i] == 1;
    ref_[i] = keep ? ref_[i] + alpha * (scene_[i] - ref_[i]) : scene_[i];
    age_[i] = keep ? age_[i] + 1 : 0;
  }
}

void FrameStripes::applyPatternChange(const std::vector<float>& dcol, const std::vector<float>& drow) {
  if (!started_ || int(dcol.size()) != W || int(drow.size()) != H) return;
  // Scene coordinate q holds the sensor's pixel q + cum.
  auto at = [](const std::vector<float>& v, double pos) {
    const int n = int(v.size());
    pos = std::clamp(pos, 0.0, double(n - 1));
    const int i = std::min(int(pos), n - 2);
    const double f = pos - i;
    return float((1.0 - f) * v[size_t(i)] + f * v[size_t(i + 1)]);
  };
  std::vector<float> col(W), row(H);
  for (int x = 0; x < W; ++x) col[size_t(x)] = at(dcol, x + cum_[0]);
  for (int y = 0; y < H; ++y) row[size_t(y)] = at(drow, y + cum_[1]);
  for (int y = 0; y < H; ++y) {
    float* r = ref_.data() + size_t(y) * W;
    for (int x = 0; x < W; ++x) r[x] -= col[size_t(x)] + row[size_t(y)];
  }
}

}  // namespace tv
