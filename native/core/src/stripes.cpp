#include "tv/stripes.h"

#include <algorithm>
#include <cmath>

#include "tv/frame.h"

namespace tv {
namespace {

constexpr int W = kFrameWidth, H = kImageRows;

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
                              std::vector<float>& gy, std::vector<float>& bw, std::vector<float>& sample) {
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
    double axx = 0, axy = 0, ayy = 0, bx = 0, by = 0;
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
        axx += wt * ix * ix;
        axy += wt * ix * iy;
        ayy += wt * iy * iy;
        bx += wt * ix * r;
        by += wt * iy * r;
      }
    }
    const double det = axx * ayy - axy * axy, tr = axx + ayy;
    if (!(det > 1e-12 * tr * tr) || !(tr > 0)) break;  // (too little structure to tell)
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
  for (int y = 0; y < H; ++y) {
    const int* row = in + size_t(reflect101(y - iy, H)) * W;
    int* o = out + size_t(y) * W;
    for (int x = 0; x < W; ++x) o[x] = row[reflect101(x - ix, W)];
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
  for (int y = 0; y < h; ++y) {
    const float* row = in + size_t(y) * w;
    float* o = scratch.data() + size_t(y) * w;
    for (int x = 0; x < w; ++x) {
      const int x0 = x + ix;
      o[x] = wx[0] * row[reflect101(x0 - 1, w)] + wx[1] * row[reflect101(x0, w)] + wx[2] * row[reflect101(x0 + 1, w)] +
             wx[3] * row[reflect101(x0 + 2, w)];
    }
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
                                    std::array<double, 2> guess, double noise) {
  // At half resolution and below, each level less its column and row means so stripes can't read as
  // motion. Accurate to ~0.07 px at worst (Lucas-Kanade's ~0.035 px at its finest level, half the
  // frame's; measured with exact Fourier shifts): through a pan the stripe fix does as well with it as
  // with the true motion, and a full-resolution level would cost ~1 ms more on the tablet.
  constexpr int kLevels = 3;  // 128 x 96, 64 x 48, 32 x 24
  std::vector<std::vector<float>> pa(kLevels), pb(kLevels);
  std::vector<int> ws(kLevels), hs(kLevels);
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
  std::vector<float> gx, gy, bw, sample;
  std::array<double, 2> d{guess[0] / double(2 << (kLevels - 1)), guess[1] / double(2 << (kLevels - 1))};
  for (int l = kLevels - 1; l >= 0; --l) {
    // (each level's noise: the pyramid's blur takes it down ~2x a level; residuals carry two images')
    const double floor = noise * 1.4142 / double(2 << l);
    d = lkLevel(pa[size_t(l)].data(), pb[size_t(l)].data(), ws[size_t(l)], hs[size_t(l)], d, l == 0 ? 6 : 5, floor,
                scratch, gx, gy, bw, sample);
    d[0] *= 2.0;  // (each level is half the one below; level 0 is half the frame)
    d[1] *= 2.0;
  }
  return d;
}

void FrameStripes::reset() {
  started_ = false;
  updatePending_ = false;
  cum_[0] = cum_[1] = 0.0;
  lastShift_ = {0.0f, 0.0f};
  lastMatched_ = 0.0f;
  lastCorrected_ = false;
}

void FrameStripes::process(float* sig, float sigma) {
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
  uncorrected_.assign(sig, sig + kImagePixels);  // (stage 3b learns from it; the reference follows it)
  if (!started_) {  // the first frame (or after a restart): the reference begins here
    std::copy(sig, sig + kImagePixels, ref_.begin());
    std::fill(age_.begin(), age_.end(), 0);
    cum_[0] = cum_[1] = 0.0;
    started_ = true;
    lastMatched_ = 0.0f;
    return;
  }
  // Where the scene went on the sensor since the reference last saw it.
  shiftImage(ref_.data(), refSensor_.data(), W, H, cum_[0], cum_[1], tmp_);
  const std::array<double, 2> delta = estimateShift(refSensor_.data(), sig, tmp_, {0.0, 0.0}, sigma);
  if (std::max(std::fabs(delta[0]), std::fabs(delta[1])) > o.minShift) {
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
  for (size_t i = 0; i < kImagePixels; ++i) diff_[i] = sig[i] - refSensor_[i];
  values_.clear();
  for (size_t i = 5; i < kImagePixels; i += 23) values_.push_back(diff_[i]);  // (a subsample: every column and row)
  const float med = medianOf(values_);
  const float g = o.gate * sigma * 1.4f;
  const float edge = o.edge * sigma;
  // One pass: the shared offset off, the gate, freshness, and (a misalignment of a few hundredths of
  // a pixel during a pan would leave a residual along a strong vertical or horizontal edge, under the
  // gate, that its column or row would take for an offset) the edges; then each column's and row's
  // first mean over the pixels that pass.
  std::vector<double> colSum(W, 0.0), rowSum(H, 0.0);
  std::vector<int> colCount(W, 0), rowCount(H, 0);
  size_t unchanged = 0;
  const float* r = refSensor_.data();
  for (int y = 0; y < H; ++y) {
    float* d = diff_.data() + size_t(y) * W;
    const float* rr = r + size_t(y) * W;
    const float* up = r + size_t(y > 0 ? y - 1 : y) * W;
    const float* dn = r + size_t(y < H - 1 ? y + 1 : y) * W;
    const int* ag = ageSensor_.data() + size_t(y) * W;
    unsigned char* mk = matched_.data() + size_t(y) * W;
    double rs = 0.0;
    int rc = 0;
    for (int x = 0; x < W; ++x) {
      d[x] -= med;  // the frame's shared offset is not a stripe
      const bool still = std::fabs(d[x]) < g;
      unchanged += still;
      const int xl = x > 0 ? x - 1 : x, xr = x < W - 1 ? x + 1 : x;
      const bool flat = std::fabs(rr[xr] - rr[xl]) < 2.0f * edge && std::fabs(dn[x] - up[x]) < 2.0f * edge;
      const bool vote = still && flat && ag[x] >= o.fresh;
      mk[x] = vote;
      if (vote) {
        colSum[size_t(x)] += d[x];
        ++colCount[size_t(x)];
        rs += d[x];
        ++rc;
      }
    }
    rowSum[size_t(y)] = rs;
    rowCount[size_t(y)] = rc;
  }
  lastMatched_ = float(unchanged) / float(kImagePixels);
  if (lastMatched_ < o.lost) {  // can't follow the scene: start over from this frame
    std::copy(sig, sig + kImagePixels, ref_.begin());
    std::fill(age_.begin(), age_.end(), 0);
    cum_[0] = cum_[1] = 0.0;
    return;
  }
  // Each offset: its first mean, then again over the values within band x 1.4 sigma of it (a one-step
  // M-estimate: as robust as a trimmed mean here, the gate having bounded every value, and cheaper).
  // Columns first; the rows from what the columns leave.
  colOff_.assign(W, 0.0f);
  rowOff_.assign(H, 0.0f);
  const float band = o.band * 1.4f * sigma;
  std::vector<float> first(W);
  for (int x = 0; x < W; ++x) first[size_t(x)] = colCount[size_t(x)] ? float(colSum[size_t(x)] / colCount[size_t(x)]) : 0.0f;
  std::fill(colSum.begin(), colSum.end(), 0.0);
  std::fill(colCount.begin(), colCount.end(), 0);
  for (int y = 0; y < H; ++y) {
    const float* d = diff_.data() + size_t(y) * W;
    const unsigned char* mk = matched_.data() + size_t(y) * W;
    for (int x = 0; x < W; ++x)
      if (mk[x] && std::fabs(d[x] - first[size_t(x)]) < band) {
        colSum[size_t(x)] += d[x];
        ++colCount[size_t(x)];
      }
  }
  for (int x = 0; x < W; ++x)
    colOff_[size_t(x)] = colCount[size_t(x)] >= H / 2 ? float(colSum[size_t(x)] / colCount[size_t(x)]) : 0.0f;
  for (int y = 0; y < H; ++y) {
    const float* d = diff_.data() + size_t(y) * W;
    const unsigned char* mk = matched_.data() + size_t(y) * W;
    double s1 = 0.0, s2 = 0.0;
    int n1 = 0, n2 = 0;
    for (int x = 0; x < W; ++x)
      if (mk[x]) {
        s1 += d[x] - colOff_[size_t(x)];
        ++n1;
      }
    const float f1 = n1 ? float(s1 / n1) : 0.0f;
    for (int x = 0; x < W; ++x) {
      const float v = d[x] - colOff_[size_t(x)];
      if (mk[x] && std::fabs(v - f1) < band) {
        s2 += v;
        ++n2;
      }
    }
    rowOff_[size_t(y)] = n2 >= W / 2 ? float(s2 / n2) : 0.0f;
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
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const float c = std::clamp(colOff_[size_t(x)] + rowOff_[size_t(y)], -o.clamp, o.clamp);
      sig[size_t(y) * W + x] -= c;
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
    if (flagsScene_[i] == 1) ref_[i] += alpha * (scene_[i] - ref_[i]);
    else ref_[i] = scene_[i];
    age_[i] = flagsScene_[i] == 1 ? age_[i] + 1 : 0;
  }
}

}  // namespace tv
