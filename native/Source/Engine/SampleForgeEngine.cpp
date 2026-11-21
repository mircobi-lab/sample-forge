// Sample Forge · MBStudio91 — motore DSP nativo. Vedi SampleForgeEngine.h
#include "SampleForgeEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace sf
{
namespace
{
constexpr int N = 2048, HOP = 512, B = N / 2 + 1;
constexpr double PI = 3.14159265358979323846;

// Math.round di JavaScript (metà verso +infinito)
inline long jsRound (double x) { return (long) std::floor (x + 0.5); }
inline double clamp01 (double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
inline double smoothstep01 (double t) { t = clamp01 (t); return t * t * (3 - 2 * t); }

struct Cancelled {};

const char* NAMES_S[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* NAMES_F[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
const char* NAMES_IT[12] = { "Do", "Do#", "Re", "Mi\xe2\x99\xad", "Mi", "Fa", "Fa#", "Sol", "La\xe2\x99\xad", "La", "Si\xe2\x99\xad", "Si" };

std::string fmtT (double t)
{
    int m = (int) std::floor (t / 60);
    double s = t - m * 60;
    char buf[32];
    std::snprintf (buf, sizeof buf, "%d:%s%.1f", m, s < 10 ? "0" : "", s);
    return buf;
}
std::string fmt1 (double v) { char b[32]; std::snprintf (b, sizeof b, "%.1f", v); return b; }

// percentile come nel JS: ordina e prende l'elemento floor(n*q)
template <typename V>
double percentile (const V& arr, double q)
{
    std::vector<float> a (arr.begin(), arr.end());
    if (a.empty()) return 0;
    std::sort (a.begin(), a.end());
    size_t i = std::min (a.size() - 1, (size_t) std::floor (a.size() * q));
    return a[i];
}

std::vector<double> prefix (const std::vector<float>& x)
{
    std::vector<double> p (x.size() + 1, 0.0);
    for (size_t i = 0; i < x.size(); ++i) p[i + 1] = p[i] + x[i];
    return p;
}

std::vector<int> peaks (const std::vector<float>& x, int w, int meanW, double deltaRel)
{
    const int n = (int) x.size();
    auto P = prefix (x);
    const double delta = deltaRel * percentile (x, 0.95);
    std::vector<int> out;
    long last = -1000000000;
    for (int i = 1; i < n - 1; ++i)
    {
        const double v = x[i];
        if (v <= 0 || v <= x[i - 1]) continue;
        bool ok = true;
        for (int j = std::max (0, i - w); j <= std::min (n - 1, i + w); ++j)
            if (x[j] > v) { ok = false; break; }
        if (! ok) continue;
        const int a = std::max (0, i - meanW), b = std::min (n, i + meanW + 1);
        const double mean = (P[b] - P[a]) / (b - a);
        if (v >= mean * 1.25 + delta && i - last >= w) { out.push_back (i); last = i; }
    }
    return out;
}

double interp (const std::vector<float>& x, double p)
{
    const long i = (long) std::floor (p);
    if (i < 0 || i >= (long) x.size() - 1) return 0;
    const double fr = p - i;
    return x[i] * (1 - fr) + x[i + 1] * fr;
}

float qsel (float* a, int n, int k)
{
    int l = 0, r = n - 1;
    while (r > l)
    {
        const float pv = a[(l + r) >> 1];
        int i = l, j = r;
        while (i <= j)
        {
            while (a[i] < pv) i++;
            while (a[j] > pv) j--;
            if (i <= j) { std::swap (a[i], a[j]); i++; j--; }
        }
        if (k <= j) r = j; else if (k >= i) l = i; else break;
    }
    return a[k];
}

struct FFT
{
    std::vector<uint32_t> rev;
    std::vector<double> c, s;
    int n;
    explicit FFT (int size) : n (size)
    {
        const int lv = (int) std::log2 (n);
        rev.resize (n);
        for (int i = 0; i < n; ++i) { int r = 0, x = i; for (int j = 0; j < lv; ++j) { r = (r << 1) | (x & 1); x >>= 1; } rev[i] = (uint32_t) r; }
        c.resize (n / 2); s.resize (n / 2);
        for (int i = 0; i < n / 2; ++i) { c[i] = std::cos (2 * PI * i / n); s[i] = std::sin (2 * PI * i / n); }
    }
    void run (double* xr, double* xi, bool inv) const
    {
        for (int i = 0; i < n; ++i) { const int j = (int) rev[i]; if (j > i) { std::swap (xr[i], xr[j]); std::swap (xi[i], xi[j]); } }
        for (int size = 2; size <= n; size <<= 1)
        {
            const int h = size >> 1, st = n / size;
            for (int i = 0; i < n; i += size)
                for (int j = i, k = 0; j < i + h; ++j, k += st)
                {
                    const double wr = c[k], wi = inv ? s[k] : -s[k];
                    const double ar = xr[j + h], ai = xi[j + h];
                    const double tr = ar * wr - ai * wi, ti = ar * wi + ai * wr;
                    xr[j + h] = xr[j] - tr; xi[j + h] = xi[j] - ti; xr[j] += tr; xi[j] += ti;
                }
        }
        if (inv) for (int i = 0; i < n; ++i) { xr[i] /= n; xi[i] /= n; }
    }
};

struct Spec { std::vector<double> Lr, Li, Rr, Ri; Spec() : Lr (B), Li (B), Rr (B), Ri (B) {} };

struct Grid { double bpm = 120, T = 0.5, t0 = 0; int d = 0, nBeats = 0, nBars = 0; };

struct Feat
{
    std::vector<float> fluxFull, fluxPerc, fluxVoc, fluxBass, eTot, ePerc, eVoc, eBass, eInst, pb, chroma;
    std::vector<double> key;
};

struct DrumItem
{
    int f; double t; double sh[4]; double s, ratio, decay;
    double gapN = 9, gapP = 9, nextS = 0, score = 0;
    std::string cls;
};
}  // namespace

// =====================================================================================
struct Engine::Impl
{
    Params& P;
    explicit Impl (Params& p) : P (p), fft (N), win (N), re (N), im (N)
    {
        for (int i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * std::cos (2 * PI * i / N);
    }

    FFT fft;
    std::vector<double> win;
    mutable std::vector<double> re, im;

    double SR = 44100, fps = 0, cappedSec = 0;
    long LEN = 0;
    int F = 0;
    bool stems = false;
    Sources src;
    std::vector<float> mag;
    std::vector<uint8_t> cw, msk;
    std::vector<float> bandB, bandV;
    std::vector<int8_t> pcOfBin;
    std::vector<uint8_t> bandIdx;
    Feat feat;
    Grid grid;
    KeyInfo keyInfo;
    std::array<double, 24> keyCorr {};
    std::vector<std::string> progression;
    Progress progressCb;
    const std::atomic<bool>* cancel = nullptr;

    void progress (double p, const char* msg)
    {
        if (cancel && cancel->load()) throw Cancelled();
        if (progressCb) progressCb (p, msg);
    }

    // ---------------------------------------------------------------- spettri
    void spectra (const Stereo& x, int f, Spec& sp) const
    {
        const long s = (long) f * HOP - N / 2;
        const long len = (long) x.L.size();
        for (int i = 0; i < N; ++i)
        {
            const long t = s + i;
            if (t >= 0 && t < len) { re[i] = x.L[t] * win[i]; im[i] = x.R[t] * win[i]; }
            else { re[i] = 0; im[i] = 0; }
        }
        fft.run (re.data(), im.data(), false);
        for (int k = 0; k < B; ++k)
        {
            const int nk = (N - k) & (N - 1);
            const double zr = re[k], zi = im[k], znr = re[nk], zni = im[nk];
            sp.Lr[k] = (zr + znr) / 2; sp.Li[k] = (zi - zni) / 2; sp.Rr[k] = (zi + zni) / 2; sp.Ri[k] = -(zr - znr) / 2;
        }
    }

    void setupBins()
    {
        bandB.assign (B, 0); bandV.assign (B, 0); pcOfBin.assign (B, -1); bandIdx.assign (B, 0);
        for (int k = 0; k < B; ++k)
        {
            const double hz = k * SR / N;
            bandB[k] = (float) (hz < 150 ? 1 : hz > 250 ? 0 : 0.5 + 0.5 * std::cos (PI * (hz - 150) / 100));
            const double v = hz < 120 ? 0 : hz < 220 ? 0.5 - 0.5 * std::cos (PI * (hz - 120) / 100) : hz < 9000 ? 1 : hz < 13000 ? 1 - 0.75 * (hz - 9000) / 4000 : 0.25;
            bandV[k] = (float) std::min (v, 1.0 - bandB[k]);
            pcOfBin[k] = (int8_t) ((hz >= 130 && hz <= 4200) ? ((((jsRound (12 * std::log2 (hz / 440)) + 69) % 12) + 12) % 12) : -1);
            bandIdx[k] = (uint8_t) (hz < 150 ? 0 : hz < 800 ? 1 : hz < 4000 ? 2 : 3);
        }
    }

    // ---------------------------------------------------------------- separatore
    void pass1()
    {
        mag.assign ((size_t) F * B, 0.f); cw.assign ((size_t) F * B, 0);
        Spec sp;
        std::vector<double> tx (B + 1, 0), ty (B + 1, 0), tz (B + 1, 0), Sxy (B, 0), Sxx (B, 0), Syy (B, 0);
        const double a = P.cSmoothT; const int W = P.cSmoothF;
        for (int f = 0; f < F; ++f)
        {
            spectra (src.mix, f, sp);
            const size_t base = (size_t) f * B;
            for (int k = 0; k < B; ++k)
            {
                const double a2 = sp.Lr[k] * sp.Lr[k] + sp.Li[k] * sp.Li[k], b2 = sp.Rr[k] * sp.Rr[k] + sp.Ri[k] * sp.Ri[k];
                mag[base + k] = (float) std::sqrt ((a2 + b2) / 2);
                tx[k + 1] = tx[k] + (sp.Lr[k] * sp.Rr[k] + sp.Li[k] * sp.Ri[k]); ty[k + 1] = ty[k] + a2; tz[k + 1] = tz[k] + b2;
            }
            for (int k = 0; k < B; ++k)
            {
                const int lo = std::max (0, k - W), hi = std::min (B, k + W + 1);
                Sxy[k] = a * Sxy[k] + (1 - a) * (tx[hi] - tx[lo]);
                Sxx[k] = a * Sxx[k] + (1 - a) * (ty[hi] - ty[lo]);
                Syy[k] = a * Syy[k] + (1 - a) * (tz[hi] - tz[lo]);
                const double den = Sxx[k] + Syy[k];
                const double c = den < 1e-18 ? 1 : std::max (0.0, 2 * Sxy[k] / den);
                cw[base + k] = (uint8_t) jsRound (255 * std::pow (std::min (1.0, c), P.csharp));
            }
            if ((f & 255) == 0) progress (0.03 + 0.2 * f / F, "Analisi spettrale");
        }
    }

    void buildMasks()
    {
        msk.assign ((size_t) F * B * 4, 0);
        std::vector<float> bs (P.medS + 1), bl (P.medL + 2), bp (P.medP + 1);
        const int hs = P.medS >> 1, hl = P.medL >> 1, hp = P.medP >> 1;
        const double q = P.q, binHz = SR / N;
        std::vector<float> aHs (B), aHl (B), aPf (B), comb (B);
        const int kickBins = std::min ((B - 2) / 3, (int) jsRound (P.kickHz / binHz));
        auto clampF = [this] (int g) { return g < 0 ? 0 : g >= F ? F - 1 : g; };
        for (int f = 0; f < F; ++f)
        {
            const size_t base = (size_t) f * B;
            for (int k = 0; k < B; ++k)
            {
                int n = 0;
                for (int g = f - hs; g <= f + hs; ++g) bs[n++] = mag[(size_t) clampF (g) * B + k];
                aHs[k] = qsel (bs.data(), n, n >> 1);
                n = 0;
                for (int g = f - hl; g <= f + hl; g += P.medLstep) bl[n++] = mag[(size_t) clampF (g) * B + k];
                aHl[k] = qsel (bl.data(), n, n >> 1);
                n = 0;
                for (int x = k - hp; x <= k + hp; ++x) bp[n++] = mag[base + (x < 0 ? 0 : x >= B ? B - 1 : x)];
                aPf[k] = qsel (bp.data(), n, n >> 1);
            }
            auto kickAt = [&] (int k)
            {
                const double h2 = mag[base + 2 * k];
                const double h3 = std::max ((double) mag[base + 3 * k], 0.5 * (mag[base + 3 * k - 1] + mag[base + 3 * k + 1]));
                double t = ((h2 + h3) / (mag[base + k] + 1e-12) - P.supLo) / (P.supHi - P.supLo);
                return 1 - smoothstep01 (t);
            };
            for (int k = 0; k < B; ++k) comb[k] = (float) (k <= kickBins ? kickAt (std::max (2, k)) : 0);
            for (int k = 0; k < B; ++k)
            {
                const double Hs = aHs[k], Hl = aHl[k], Pf = aPf[k];
                const double pq = std::pow (Pf, q), hsq = std::pow (Hs, q), hlq = std::pow (Hl, q), hm = std::pow (P.pMargin * Hs, q);
                const double mp = pq / (pq + hm + 1e-20), mh = 1 - mp;
                const double s = std::min (1.0, hlq / (hsq + 1e-20));
                const double bb = bandB[k];
                double wv = (cw[base + k] / 255.0) * bandV[k] * (P.fluctW * (1 - s) + P.sustW * s);
                if (wv > 1 - bb) wv = 1 - bb;
                const double isBass = 1 - comb[k];
                const size_t o = (base + k) * 4;
                const double d = mp + mh * bb * (1 - isBass), v = mh * wv, b = mh * bb * isBass;
                const int m0 = (int) jsRound (255 * d), m1 = (int) jsRound (255 * v), m2 = (int) jsRound (255 * b);
                msk[o] = (uint8_t) m0; msk[o + 1] = (uint8_t) m1; msk[o + 2] = (uint8_t) m2;
                msk[o + 3] = (uint8_t) std::max (0, 255 - msk[o] - msk[o + 1] - msk[o + 2]);
            }
            if ((f & 127) == 0) progress (0.23 + 0.32 * f / F, "Separazione batteria / voce / basso / accordi");
        }
    }

    void smoothMasks()
    {
        const int T = P.smoothT, W = P.smoothF;
        const int kLo = (int) jsRound (P.smoothLoHz * N / SR);
        const int R = B * 4;
        std::vector<float> row (R), prev (R), cur (R), acc (R);
        if (W > 0)
            for (int f = 0; f < F; ++f)
            {
                const size_t base = (size_t) f * R;
                for (int i = 0; i < R; ++i) row[i] = msk[base + i];
                for (int k = kLo; k < B; ++k)
                    for (int s = 0; s < 4; ++s)
                    {
                        double sum = 0; int n = 0;
                        for (int x = std::max (0, k - W); x <= std::min (B - 1, k + W); ++x) { sum += row[x * 4 + s]; n++; }
                        msk[base + k * 4 + s] = (uint8_t) jsRound (sum / n);
                    }
            }
        for (int i = 0; i < R; ++i) prev[i] = msk[i];
        for (int f = 0; f < F; ++f)
        {
            const size_t base = (size_t) f * R, nb = (size_t) std::min (F - 1, f + 1) * R;
            for (int i = 0; i < R; ++i) cur[i] = msk[base + i];
            if (T == 0) for (int i = 0; i < R; ++i) acc[i] = cur[i];
            else for (int i = 0; i < R; ++i) acc[i] = (float) (0.25 * prev[i] + 0.5 * cur[i] + 0.25 * msk[nb + i]);
            for (int i = 0; i < R; ++i) prev[i] = cur[i];
            for (int k = 0; k < B; ++k)
            {
                const int o = k * 4;
                const double a0 = std::pow ((double) acc[o], P.sharp), a1 = std::pow ((double) acc[o + 1], P.sharp),
                             a2 = std::pow ((double) acc[o + 2], P.sharp), a3 = std::pow ((double) acc[o + 3], P.sharp);
                double sum = a0 + a1 + a2 + a3; if (sum == 0) sum = 1;
                const int d = (int) jsRound (255 * a0 / sum), v = (int) jsRound (255 * a1 / sum), b = (int) jsRound (255 * a2 / sum);
                msk[base + o] = (uint8_t) d; msk[base + o + 1] = (uint8_t) v; msk[base + o + 2] = (uint8_t) b;
                msk[base + o + 3] = (uint8_t) std::max (0, 255 - d - v - b);
            }
        }
        progress (0.57, "Pulizia delle maschere");
    }

    void vocalGate()
    {
        std::vector<float> r (F);
        for (int f = 0; f < F; ++f)
        {
            double ev = 0, et = 0; const size_t base = (size_t) f * B;
            for (int k = 0; k < B; ++k) { const double m = mag[base + k], v = m * msk[(base + k) * 4 + 1] / 255.0; ev += v * v; et += m * m; }
            r[f] = (float) (ev / (et + 1e-20));
        }
        const int w = std::max (1, (int) jsRound (P.vGateWin * fps));
        auto Pr = prefix (r);
        for (int f = 0; f < F; ++f)
        {
            const int a = std::max (0, f - w), b = std::min (F, f + w + 1);
            const double x = (Pr[b] - Pr[a]) / (b - a);
            const double g = smoothstep01 ((x - P.vGateLo) / (P.vGateHi - P.vGateLo));
            if (g >= 0.999) continue;
            for (int k = 0; k < B; ++k)
            {
                const size_t o = ((size_t) f * B + k) * 4;
                const int v = msk[o + 1]; if (! v) continue;
                const int keep = (int) jsRound (v * g), give = v - keep, rest = msk[o] + msk[o + 2] + msk[o + 3];
                msk[o + 1] = (uint8_t) keep;
                if (rest > 0)
                {
                    const int d = (int) jsRound ((double) give * msk[o] / rest), bb = (int) jsRound ((double) give * msk[o + 2] / rest);
                    msk[o] = (uint8_t) (msk[o] + d); msk[o + 2] = (uint8_t) (msk[o + 2] + bb);
                    msk[o + 3] = (uint8_t) (255 - msk[o] - msk[o + 1] - msk[o + 2]);
                }
                else msk[o + 3] = (uint8_t) (msk[o + 3] + give);
            }
        }
    }

    // ---------------------------------------------------------------- descrittori
    void pass2()
    {
        auto z = [this] { return std::vector<float> ((size_t) F, 0.f); };
        feat.fluxFull = z(); feat.fluxPerc = z(); feat.fluxVoc = z(); feat.fluxBass = z();
        feat.eTot = z(); feat.ePerc = z(); feat.eVoc = z(); feat.eBass = z(); feat.eInst = z();
        feat.pb.assign ((size_t) F * 4, 0.f); feat.chroma.assign ((size_t) F * 12, 0.f); feat.key.assign (12, 0.0);
        std::vector<float> ot (B), op (B), ov (B), ob (B), oi (B), oh (B);
        std::vector<float> pt (B, 0), pp (B, 0), pv (B, 0), pbv (B, 0);
        Spec sp; std::vector<float> md (B), mv (B), mb (B), mo (B);
        auto stemMag = [&] (const Stereo& s, int f, std::vector<float>& out)
        {
            spectra (s, f, sp);
            for (int k = 0; k < B; ++k) out[k] = (float) std::sqrt ((sp.Lr[k] * sp.Lr[k] + sp.Li[k] * sp.Li[k] + sp.Rr[k] * sp.Rr[k] + sp.Ri[k] * sp.Ri[k]) / 2);
        };
        for (int f = 0; f < F; ++f)
        {
            if (! stems)
            {
                const size_t base = (size_t) f * B;
                for (int k = 0; k < B; ++k)
                {
                    const size_t x = (base + k) * 4;
                    const double m = mag[base + k], d = msk[x] / 255.0, v = msk[x + 1] / 255.0, b = msk[x + 2] / 255.0, c = msk[x + 3] / 255.0;
                    ot[k] = (float) m; op[k] = (float) (m * d); ov[k] = (float) (m * v); ob[k] = (float) (m * b); oi[k] = (float) (m * (c + b)); oh[k] = (float) (m * (1 - d));
                }
            }
            else
            {
                stemMag (src.drums, f, md); stemMag (src.vocals, f, mv); stemMag (src.bass, f, mb); stemMag (src.other, f, mo);
                for (int k = 0; k < B; ++k)
                {
                    const double d = md[k], v = mv[k], b = mb[k], x = mo[k];
                    ot[k] = (float) std::sqrt (d * d + v * v + b * b + x * x); op[k] = (float) d; ov[k] = (float) v; ob[k] = (float) b;
                    oi[k] = (float) std::sqrt (b * b + x * x); oh[k] = (float) std::sqrt (b * b + x * x + v * v);
                }
            }
            double ff = 0, fp = 0, fv = 0, fb = 0, et = 0, ep = 0, ev = 0, eb = 0, ei = 0;
            const size_t cb = (size_t) f * 12, pbb = (size_t) f * 4;
            for (int k = 1; k < B; ++k)
            {
                const float lt = (float) std::log1p (ot[k]), lp = (float) std::log1p (op[k]), lv = (float) std::log1p (ov[k]), lb = (float) std::log1p (ob[k]);
                double d = lt - pt[k]; if (d > 0) ff += d; pt[k] = lt;
                d = lp - pp[k]; if (d > 0) fp += d; pp[k] = lp;
                d = lv - pv[k]; if (d > 0) fv += d; pv[k] = lv;
                d = lb - pbv[k]; if (d > 0) fb += d; pbv[k] = lb;
                const double p2 = (double) op[k] * op[k];
                et += (double) ot[k] * ot[k]; ep += p2; ev += (double) ov[k] * ov[k]; eb += (double) ob[k] * ob[k]; ei += (double) oi[k] * oi[k];
                feat.pb[pbb + bandIdx[k]] += (float) p2;
                const int pc = pcOfBin[k];
                if (pc >= 0) { feat.chroma[cb + pc] += oi[k]; feat.key[pc] += oh[k]; }
            }
            feat.fluxFull[f] = (float) ff; feat.fluxPerc[f] = (float) fp; feat.fluxVoc[f] = (float) fv; feat.fluxBass[f] = (float) fb;
            feat.eTot[f] = (float) et; feat.ePerc[f] = (float) ep; feat.eVoc[f] = (float) ev; feat.eBass[f] = (float) eb; feat.eInst[f] = (float) ei;
            if ((f & 127) == 0) progress (0.6 + 0.13 * f / F, stems ? "Analisi degli stem" : "Calcolo dei descrittori");
        }
    }

    // ---------------------------------------------------------------- tempo
    std::vector<float> onsetEnv() const
    {
        const auto& x = feat.fluxFull; const int n = (int) x.size();
        auto Pp = prefix (x);
        const int w = (int) jsRound (fps * 0.25);
        std::vector<float> env (n);
        for (int i = 0; i < n; ++i)
        {
            const int a = std::max (0, i - w), b = std::min (n, i + w + 1);
            env[i] = (float) std::max (0.0, x[i] - (Pp[b] - Pp[a]) / (b - a));
        }
        return env;
    }
    static std::pair<double, double> bestPhase (const std::vector<float>& env, double Tf)
    {
        double best = -1, bp = 0;
        for (double ph = 0; ph < Tf; ph += 0.5)
        {
            double s = 0;
            for (double p = ph; p < env.size(); p += Tf) s += interp (env, p);
            if (s > best) { best = s; bp = ph; }
        }
        return { best, bp };
    }

    void makeGrid (double forcedBpm)
    {
        auto env = onsetEnv();
        double bpm, ph;
        const double dur = (double) LEN / SR;
        if (forcedBpm > 0) { bpm = forcedBpm; ph = bestPhase (env, fps * 60 / bpm).second; }
        else
        {
            const int n = (int) env.size();
            double best = -1; int bestLag = 0;
            for (int lag = (int) std::floor (fps * 60 / 200); lag <= (int) std::ceil (fps * 60 / 60); ++lag)
            {
                double s = 0;
                for (int i = 0; i + lag < n; ++i) s += (double) env[i] * env[i + lag];
                s /= std::max (1, n - lag);
                const double b = 60 * fps / lag, w = std::exp (-0.5 * std::pow (std::log2 (b / 120) / 0.9, 2));
                if (s * w > best) { best = s * w; bestLag = lag; }
            }
            double b0 = best > 0 ? 60 * fps / bestLag : 120;
            while (b0 < 78) b0 *= 2;
            while (b0 > 165) b0 /= 2;
            double bs = -1; bpm = b0; ph = 0;
            for (double b = b0 - 3.5; b <= b0 + 3.5; b += 0.02)
            {
                auto r = bestPhase (env, fps * 60 / b);
                if (r.first > bs) { bs = r.first; bpm = b; ph = r.second; }
            }
            const double rb = (double) jsRound (bpm);
            if (std::abs (bpm - rb) < 0.35) { auto r = bestPhase (env, fps * 60 / rb); if (r.first >= 0.97 * bs) { bpm = rb; ph = r.second; } }
            bpm = jsRound (bpm * 100) / 100.0;
        }
        const double T = 60 / bpm;
        double t0 = ph / fps; if (t0 > 0.75 * T) t0 -= T;
        const int nBeats = std::max (0, (int) std::floor ((dur - t0) / T));
        std::vector<float> chB ((size_t) nBeats * 12, 0.f), lowB (nBeats, 0.f);
        for (int b = 0; b < nBeats; ++b)
        {
            const long f0 = std::max (0L, jsRound ((t0 + b * T) * fps)), f1 = jsRound ((t0 + (b + 1) * T) * fps);
            for (long f = f0; f < f1 && f < F; ++f) for (int p = 0; p < 12; ++p) chB[b * 12 + p] += feat.chroma[f * 12 + p];
            double low = 0;
            for (long g = std::max (0L, f0 - 2); g <= std::min ((long) F - 1, f0 + 2); ++g) low = std::max (low, (double) feat.pb[g * 4]);
            lowB[b] = (float) std::sqrt (low);
        }
        auto win2 = [&] (int b, int dir)
        {
            std::array<double, 12> v {};
            for (int x = 0; x < 2; ++x) { const int bb = dir > 0 ? b + x : b - 1 - x; if (bb >= 0 && bb < nBeats) for (int p = 0; p < 12; ++p) v[p] += chB[bb * 12 + p]; }
            return v;
        };
        auto cosd = [] (const std::array<double, 12>& a, const std::array<double, 12>& c)
        {
            double n = 0, x = 0, y = 0;
            for (int p = 0; p < 12; ++p) { n += a[p] * c[p]; x += a[p] * a[p]; y += c[p] * c[p]; }
            return (x > 0 && y > 0) ? 1 - n / std::sqrt (x * y) : 0.0;
        };
        std::vector<double> nov (nBeats, 0);
        for (int b = 1; b < nBeats; ++b) nov[b] = cosd (win2 (b, -1), win2 (b, 1));
        double sumC[4] = {}, sumL[4] = {};
        for (int b = 0; b < nBeats; ++b) { sumC[b % 4] += nov[b]; sumL[b % 4] += lowB[b]; }
        double nC = *std::max_element (sumC, sumC + 4), nL = *std::max_element (sumL, sumL + 4);
        if (nC == 0) nC = 1;
        if (nL == 0) nL = 1;
        int d = 0; double bestD = -1;
        for (int c = 0; c < 4; ++c) { const double s = sumC[c] / nC + 0.6 * sumL[c] / nL; if (s > bestD) { bestD = s; d = c; } }
        grid = { bpm, T, t0, d, nBeats, std::max (0, (int) std::floor ((nBeats - d) / 4.0)) };
    }
    long beatSample (int b) const { return jsRound ((grid.t0 + b * grid.T) * SR); }
    int barBeat (int j) const { return grid.d + 4 * j; }
    std::vector<float> beatAgg (const std::vector<float>& arr) const
    {
        std::vector<float> out (grid.nBeats, 0.f);
        for (int b = 0; b < grid.nBeats; ++b)
        {
            const long f0 = std::max (0L, jsRound ((grid.t0 + b * grid.T) * fps)), f1 = jsRound ((grid.t0 + (b + 1) * grid.T) * fps);
            double s = 0; for (long f = f0; f < f1 && f < F; ++f) s += arr[f];
            out[b] = (float) s;
        }
        return out;
    }

    // ---------------------------------------------------------------- tonalità
    static KeyInfo makeKey (int root, bool minor, double conf)
    {
        const int fl[6] = { 2, 7, 0, 5, 10, 3 }, fm[6] = { 5, 10, 3, 8, 1, 6 };
        const int* list = minor ? fl : fm;
        bool flats = std::find (list, list + 6, root) != list + 6;
        KeyInfo k; k.root = root; k.minor = minor; k.flats = flats; k.conf = conf;
        k.name = std::string ((flats ? NAMES_F : NAMES_S)[root]) + (minor ? "m" : "");
        k.it = std::string (NAMES_IT[root]) + (minor ? " minore" : " maggiore");
        return k;
    }
    void detectKey()
    {
        const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [] (const double* a, const double* b)
        {
            double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; } ma /= 12; mb /= 12;
            double n = 0, da = 0, db = 0;
            for (int i = 0; i < 12; ++i) { n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) * (a[i] - ma); db += (b[i] - mb) * (b[i] - mb); }
            return n / std::sqrt (da * db + 1e-12);
        };
        double best = -2; int root = 0; bool minor = false;
        for (int r = 0; r < 12; ++r)
            for (int m = 0; m < 2; ++m)
            {
                double rot[12]; for (int i = 0; i < 12; ++i) rot[i] = feat.key[(i + r) % 12];
                const double c = corr (rot, m ? mnr : maj);
                keyCorr[(m ? 12 : 0) + r] = c;
                if (c > best) { best = c; root = r; minor = m == 1; }
            }
        keyInfo = makeKey (root, minor, best);
    }
    std::string chordName (int r, bool minor) const { return std::string ((keyInfo.flats ? NAMES_F : NAMES_S)[r]) + (minor ? "m" : ""); }

    void refineKey()
    {
        std::vector<std::string> bars;
        for (auto& c : progression) if (c != "-") bars.push_back (c);
        if (bars.size() < 2) return;
        double best = -1e9; int bk = -1;
        for (int key = 0; key < 24; ++key)
        {
            const int r = key % 12; const bool minor = key >= 12;
            auto nm = [&] (int x, bool m) { return chordName ((r + x) % 12, m); };
            std::vector<std::string> dia = minor
                ? std::vector<std::string> { nm (0, true), nm (3, false), nm (5, true), nm (7, true), nm (7, false), nm (8, false), nm (10, false) }
                : std::vector<std::string> { nm (0, false), nm (2, true), nm (4, true), nm (5, false), nm (7, false), nm (9, true) };
            const std::string& tonic = dia[0];
            double inDia = 0, isTonic = 0;
            for (auto& c : bars) { if (std::find (dia.begin(), dia.end(), c) != dia.end()) inDia++; if (c == tonic) isTonic++; }
            const double sc = 2 * inDia / bars.size() + isTonic / bars.size() + (bars.front() == tonic ? 0.35 : 0) + (bars.back() == tonic ? 0.2 : 0) + 0.3 * keyCorr[key];
            if (sc > best) { best = sc; bk = key; }
        }
        const int root = bk % 12; const bool minor = bk >= 12;
        if (root == keyInfo.root && minor == keyInfo.minor) return;
        keyInfo = makeKey (root, minor, keyCorr[bk]);
    }
    bool inKey (int midi) const
    {
        static const int majS[7] = { 0, 2, 4, 5, 7, 9, 11 }, minS[8] = { 0, 2, 3, 5, 7, 8, 10, 11 };
        const int d = ((midi - keyInfo.root) % 12 + 12) % 12;
        return keyInfo.minor ? std::find (minS, minS + 8, d) != minS + 8 : std::find (majS, majS + 7, d) != majS + 7;
    }

    // ---------------------------------------------------------------- sintesi
    Stereo synth (const std::string& name, long s0, long s1) const
    {
        if (s0 < 0)
        {
            Stereo a = synthR (name, 0, s1);
            if (a.L.empty()) return {};
            Stereo o; o.L.assign (s1 - s0, 0.f); o.R.assign (s1 - s0, 0.f);
            std::copy (a.L.begin(), a.L.end(), o.L.begin() + (-s0)); std::copy (a.R.begin(), a.R.end(), o.R.begin() + (-s0));
            return o;
        }
        return synthR (name, s0, s1);
    }
    Stereo synthR (const std::string& name, long s0, long s1) const
    {
        s0 = std::max (0L, s0); s1 = std::min (LEN, s1);
        const long len = s1 - s0;
        if (len <= 16) return {};
        Stereo out; out.L.assign (len, 0.f); out.R.assign (len, 0.f);
        if (stems)
        {
            std::vector<const Stereo*> parts;
            if (name == "drums") parts = { &src.drums };
            else if (name == "vocals") parts = { &src.vocals };
            else if (name == "bass") parts = { &src.bass };
            else if (name == "chords") parts = { &src.other };
            else if (name == "harm") parts = { &src.bass, &src.other };
            else if (name == "inst") parts = { &src.drums, &src.bass, &src.other };
            else parts = { &src.drums, &src.vocals, &src.bass, &src.other };
            for (auto* p : parts) for (long i = 0; i < len; ++i) { out.L[i] += p->L[s0 + i]; out.R[i] += p->R[s0 + i]; }
            return out;
        }
        if (name == "mix")
        {
            std::copy (src.mix.L.begin() + s0, src.mix.L.begin() + s1, out.L.begin());
            std::copy (src.mix.R.begin() + s0, src.mix.R.begin() + s1, out.R.begin());
            return out;
        }
        std::vector<int> sel;
        if (name == "drums") sel = { 0 }; else if (name == "vocals") sel = { 1 }; else if (name == "bass") sel = { 2 };
        else if (name == "chords") sel = { 3 }; else if (name == "harm") sel = { 2, 3 }; else sel = { 0, 2, 3 };
        Spec sp; std::vector<double> m (B), xr (N), xi (N);
        const int fA = std::max (0, (int) std::ceil ((double) (s0 - N / 2) / HOP)), fB = std::min (F - 1, (int) std::floor ((double) (s1 + N / 2) / HOP));
        for (int f = fA; f <= fB; ++f)
        {
            spectra (src.mix, f, sp);
            for (int k = 0; k < B; ++k) { const size_t o = ((size_t) f * B + k) * 4; int v = 0; for (int j : sel) v += msk[o + j]; m[k] = v / 255.0; }
            for (int k = 0; k < B; ++k)
            {
                const double lr = sp.Lr[k] * m[k], li = sp.Li[k] * m[k], rr = sp.Rr[k] * m[k], ri = sp.Ri[k] * m[k];
                xr[k] = lr - ri; xi[k] = li + rr;
                if (k > 0 && k < N / 2) { xr[N - k] = lr + ri; xi[N - k] = -li + rr; }
            }
            fft.run (xr.data(), xi.data(), true);
            const long st = (long) f * HOP - N / 2 - s0;
            for (int i = 0; i < N; ++i)
            {
                const long t = st + i;
                if (t >= 0 && t < len) { const double w = win[i] / 1.5; out.L[t] += (float) (xr[i] * w); out.R[t] += (float) (xi[i] * w); }
            }
        }
        return out;
    }

    Stereo& fades (Stereo& a, double inMs, double outMs) const
    {
        const long n = (long) a.L.size();
        const long ni = std::min (n, jsRound (inMs * SR / 1000)), no = std::min (n, jsRound (outMs * SR / 1000));
        for (long i = 0; i < ni; ++i) { const float g = (float) i / ni; a.L[i] *= g; a.R[i] *= g; }
        for (long i = 0; i < no; ++i) { const float g = (float) i / no; a.L[n - 1 - i] *= g; a.R[n - 1 - i] *= g; }
        return a;
    }
    static bool normalize (Stereo& a)
    {
        float pk = 0;
        for (size_t i = 0; i < a.L.size(); ++i) pk = std::max ({ pk, std::abs (a.L[i]), std::abs (a.R[i]) });
        if (pk < 1e-5f) return false;
        const float g = 0.891f / pk;
        for (size_t i = 0; i < a.L.size(); ++i) { a.L[i] *= g; a.R[i] *= g; }
        return true;
    }
    void tailLowpass (Stereo& a, double t0, double t1, double hz) const
    {
        const double c = std::exp (-2 * PI * hz / SR);
        const long i0 = jsRound (t0 * SR), i1 = jsRound (t1 * SR);
        for (auto* ch : { &a.L, &a.R })
        {
            double y1 = 0, y2 = 0;
            for (long i = 0; i < (long) ch->size(); ++i)
            {
                y1 = (1 - c) * (*ch)[i] + c * y1; y2 = (1 - c) * y1 + c * y2;
                const double w = i <= i0 ? 0 : i >= i1 ? 1 : (double) (i - i0) / (i1 - i0);
                (*ch)[i] = (float) ((*ch)[i] * (1 - w) + y2 * w);
            }
        }
    }
    static Stereo slice (const Stereo& a, long from, long len)
    {
        Stereo o;
        const long end = std::min ((long) a.L.size(), from + len);
        if (from >= end) return o;
        o.L.assign (a.L.begin() + from, a.L.begin() + end); o.R.assign (a.R.begin() + from, a.R.begin() + end);
        return o;
    }
    std::shared_ptr<Pad> makePad (Stereo a, const std::string& label, const std::string& sub, double start, const std::string& kind) const
    {
        if (a.L.empty() || ! normalize (a)) return nullptr;
        auto p = std::make_shared<Pad>();
        p->label = label; p->sub = sub; p->start = start; p->kind = kind;
        p->dur = a.L.size() / SR; p->audio = std::move (a);
        return p;
    }

    // ---------------------------------------------------------------- batteria
    std::array<std::shared_ptr<Pad>, 8> selectDrums()
    {
        auto on = peaks (feat.fluxPerc, 3, (int) jsRound (fps * 0.4), 0.08);
        std::vector<DrumItem> items;
        for (int f : on)
        {
            DrumItem it {}; it.f = f; it.t = f / fps;
            double e[4] = {}, s = 0, tot = 0;
            for (int g = f; g < f + 3 && g < F; ++g) { for (int c = 0; c < 4; ++c) e[c] += feat.pb[g * 4 + c]; s += feat.ePerc[g]; tot += feat.eTot[g]; }
            const double sum = e[0] + e[1] + e[2] + e[3] + 1e-12;
            double late = 0, hi0 = 1e-12;
            for (int g = f; g < f + 3 && g < F; ++g) hi0 += feat.pb[g * 4 + 3];
            for (int g = f + 8; g < f + 11 && g < F; ++g) late += feat.pb[g * 4 + 3];
            for (int c = 0; c < 4; ++c) it.sh[c] = e[c] / sum;
            it.s = s; it.ratio = s / (tot + 1e-12); it.decay = late / hi0;
            it.gapN = 9; it.gapP = 9;
            items.push_back (it);
        }
        double maxS = 1e-12; for (auto& i : items) maxS = std::max (maxS, i.s);
        struct Proto { const char* name; double v[4]; };
        const Proto protos[4] = { { "kick", { 0.75, 0.18, 0.05, 0.02 } }, { "snare", { 0.12, 0.38, 0.35, 0.15 } }, { "hat", { 0.02, 0.06, 0.27, 0.65 } }, { "perc", { 0.25, 0.55, 0.15, 0.05 } } };
        for (size_t i = 0; i < items.size(); ++i)
        {
            auto& it = items[i];
            if (i + 1 < items.size()) { it.gapN = items[i + 1].t - it.t; it.nextS = items[i + 1].s; } else { it.gapN = 9; it.nextS = 0; }
            it.gapP = i > 0 ? it.t - items[i - 1].t : 9;
            double bd = 9;
            for (auto& p : protos) { double d = 0; for (int j = 0; j < 4; ++j) d += (p.v[j] - it.sh[j]) * (p.v[j] - it.sh[j]); if (d < bd) { bd = d; it.cls = p.name; } }
            it.score = std::sqrt (it.s / maxS) * std::min (1.0, it.gapN / 0.18) * std::min (1.0, it.gapP / 0.08) * (0.25 + it.ratio);
        }
        std::vector<int> valid;
        for (size_t i = 0; i < items.size(); ++i) if (items[i].s > 0.02 * maxS) valid.push_back ((int) i);
        std::set<int> used;
        auto pick = [&] (const std::vector<int>& list, size_t n)
        {
            std::vector<int> sorted;
            for (int i : list) if (! used.count (i)) sorted.push_back (i);
            std::stable_sort (sorted.begin(), sorted.end(), [&] (int a, int b) { return items[a].score > items[b].score; });
            std::vector<int> out;
            for (double minGap : { 4.0, 0.5 })
                for (int i : sorted)
                {
                    if (out.size() >= n) break;
                    if (std::find (out.begin(), out.end(), i) != out.end()) continue;
                    bool ok = true; for (int o : out) if (std::abs (items[o].t - items[i].t) <= minGap) { ok = false; break; }
                    if (ok) out.push_back (i);
                }
            for (int o : out) used.insert (o);
            return out;
        };
        auto byCls = [&] (const char* c) { std::vector<int> v; for (int i : valid) if (items[i].cls == c) v.push_back (i); return v; };
        auto kicks = pick (byCls ("kick"), 2), snares = pick (byCls ("snare"), 2), hats = pick (byCls ("hat"), 2), percs = pick (byCls ("perc"), 2);
        auto at = [] (const std::vector<int>& v, size_t i) { return i < v.size() ? v[i] : -1; };
        int slots[8] = { at (kicks, 0), at (kicks, 1), at (snares, 0), at (snares, 1), at (hats, 0), at (hats, 1), at (percs, 0), at (percs, 1) };
        const char* want[8] = { "kick", "kick", "snare", "snare", "hat", "hat", "perc", "perc" };
        for (int i = 0; i < 8; ++i) if (slots[i] < 0) { auto alt = pick (valid, 1); if (! alt.empty()) slots[i] = alt[0]; }
        std::map<std::string, int> counters;
        std::array<std::shared_ptr<Pad>, 8> out;
        for (int i = 0; i < 8; ++i)
        {
            if (slots[i] < 0) continue;
            const auto& it = items[slots[i]];
            double len = it.cls == "kick" ? 0.5 : it.cls == "snare" ? 0.45 : it.cls == "hat" ? (it.decay < 0.12 ? 0.2 : 0.55) : 0.4;
            if (it.nextS > 0.12 * it.s && it.gapN < len) len = std::max (it.cls == "hat" ? 0.05 : 0.1, it.gapN - 0.008);
            const double pre = 0.04;
            const long s0 = jsRound ((it.t - pre) * SR);
            Stereo raw = synth ("drums", s0, s0 + jsRound ((len + pre) * SR));
            if (raw.L.empty()) continue;
            const long look = std::min ((long) raw.L.size(), jsRound (0.12 * SR));
            float pk = 0; for (long j = 0; j < look; ++j) pk = std::max ({ pk, std::abs (raw.L[j]), std::abs (raw.R[j]) });
            long idx = 0; while (idx < look && std::max (std::abs (raw.L[idx]), std::abs (raw.R[idx])) < 0.12f * pk) idx++;
            const long st = std::max (0L, idx - jsRound (0.002 * SR));
            Stereo a = slice (raw, st, jsRound (len * SR));
            fades (a, 0.5, it.cls == "kick" ? 25 : 12);
            if (it.cls == "kick") tailLowpass (a, 0.05, 0.11, 900);
            std::string label = it.cls == "kick" ? "Cassa" : it.cls == "snare" ? "Rullante" : it.cls == "hat" ? (it.decay < 0.12 ? "Hi-hat chiuso" : "Hi-hat aperto") : "Perc";
            const int cnt = ++counters[label];
            const std::string lab = it.cls == want[i] ? label + (cnt > 1 ? " 2" : "") : label + " (alt)";
            out[i] = makePad (std::move (a), lab, fmtT (it.t), (s0 + st) / SR, "drum");
            if (out[i]) out[i]->cls = it.cls;
        }
        return out;
    }

    // ---------------------------------------------------------------- voci
    std::array<std::shared_ptr<Pad>, 8> selectVox (const std::vector<float>& bV, const std::vector<float>& bT)
    {
        std::vector<std::shared_ptr<Pad>> pads;
        double p90 = percentile (bV, 0.9); if (p90 == 0) p90 = 1e-12;
        auto von = peaks (feat.fluxVoc, 4, (int) jsRound (fps * 0.5), 0.1);
        const double chopLen = std::min (1.6, 2 * grid.T);
        struct Chop { double t, score; };
        std::vector<Chop> chops;
        for (int f : von)
        {
            double v = 0, t = 0;
            for (long g = f; g < std::min ((long) F, f + jsRound (chopLen * fps)); ++g) { v += feat.eVoc[g]; t += feat.eTot[g]; }
            chops.push_back ({ f / fps, v * (v / (t + 1e-12)) });
        }
        std::stable_sort (chops.begin(), chops.end(), [] (const Chop& a, const Chop& b) { return a.score > b.score; });
        std::vector<Chop> chosen;
        for (auto& c : chops)
        {
            if (chosen.size() >= 3) break;
            bool ok = true; for (auto& o : chosen) if (std::abs (o.t - c.t) <= 3) { ok = false; break; }
            if (ok) chosen.push_back (c);
        }
        std::stable_sort (chosen.begin(), chosen.end(), [] (const Chop& a, const Chop& b) { return a.t < b.t; });
        for (size_t i = 0; i < chosen.size(); ++i)
        {
            const long s0 = jsRound ((chosen[i].t - 0.01) * SR);
            Stereo a = synth ("vocals", s0, s0 + jsRound (chopLen * SR));
            if (a.L.empty()) { pads.push_back (nullptr); continue; }
            fades (a, 3, 30);
            pads.push_back (makePad (std::move (a), "Chop " + std::to_string (i + 1), fmtT (chosen[i].t), s0 / SR, "vox"));
        }
        while (pads.size() < 3) pads.push_back (nullptr);
        struct Cand { int j; double score; };
        auto phrase = [&] (int nb, size_t n)
        {
            std::vector<Cand> cand;
            for (int j = 0; j + nb <= grid.nBars; ++j)
            {
                double v = 0, t = 0, cov = 0; const int b0 = barBeat (j);
                for (int b = b0; b < b0 + 4 * nb; ++b) { v += bV[b]; t += bT[b]; if (bV[b] > 0.2 * p90) cov++; }
                cov /= 4 * nb;
                cand.push_back ({ j, v * (v / (t + 1e-12)) * cov * cov });
            }
            std::stable_sort (cand.begin(), cand.end(), [] (const Cand& a, const Cand& b) { return a.score > b.score; });
            std::vector<Cand> out;
            for (auto& c : cand)
            {
                if (out.size() >= n || c.score <= 0) break;
                bool ok = true; for (auto& o : out) if (! (o.j + nb <= c.j || c.j + nb <= o.j)) { ok = false; break; }
                if (ok) out.push_back (c);
            }
            std::stable_sort (out.begin(), out.end(), [] (const Cand& a, const Cand& b) { return a.j < b.j; });
            return out;
        };
        for (auto [nb, n] : { std::pair<int, size_t> { 1, 3 }, std::pair<int, size_t> { 2, 2 } })
        {
            auto ph = phrase (nb, n);
            for (size_t i = 0; i < n; ++i)
            {
                if (i >= ph.size()) { pads.push_back (nullptr); continue; }
                const auto& c = ph[i];
                const long s0 = beatSample (barBeat (c.j)), s1 = s0 + jsRound (nb * 4 * grid.T * SR);
                Stereo a = synth ("vocals", s0, s1);
                if (a.L.empty()) { pads.push_back (nullptr); continue; }
                fades (a, 3, 20);
                std::string lab = nb == 1 ? "Frase 1 battuta" : "Frase 2 battute";
                if (n > 1) lab += " " + std::to_string (i + 1);
                auto p = makePad (std::move (a), lab, "battuta " + std::to_string (c.j + 1) + " \xc2\xb7 " + fmtT (s0 / SR), s0 / SR, "vox");
                if (p) p->bars = nb;
                pads.push_back (p);
            }
        }
        std::array<std::shared_ptr<Pad>, 8> out;
        for (size_t i = 0; i < 8 && i < pads.size(); ++i) out[i] = pads[i];
        return out;
    }

    // ---------------------------------------------------------------- loop
    struct Win { int j = -1, nb = 0; bool ok() const { return j >= 0; } };
    std::array<std::shared_ptr<Pad>, 8> selectLoops (const std::vector<float>& bT, const std::vector<float>& bP, const std::vector<float>& bV,
                                                     const std::vector<float>& bB, const std::vector<float>& bI)
    {
        std::vector<std::pair<int, int>> ranges;
        auto barSum = [&] (const std::vector<float>& arr, int j, int nb)
        {
            double s = 0;
            for (int b = barBeat (j); b < barBeat (j + nb); ++b) if (b >= 0 && b < (int) arr.size()) s += arr[b];
            return s;
        };
        std::function<Win (int, std::function<double (int, int)>, bool)> best = [&] (int nb, std::function<double (int, int)> fn, bool avoid) -> Win
        {
            while (nb > 1 && nb > grid.nBars) nb >>= 1;
            int bj = -1; double bs = -1;
            for (int j = 0; j + nb <= grid.nBars; ++j)
            {
                if (avoid)
                {
                    bool clash = false;
                    for (auto& r : ranges) if (! (j + nb <= r.first || j >= r.second)) { clash = true; break; }
                    if (clash) continue;
                }
                const double s = fn (j, nb);
                if (s > bs) { bs = s; bj = j; }
            }
            if (bj < 0 && avoid) return best (nb, fn, false);
            return bj < 0 ? Win {} : Win { bj, nb };
        };
        auto mk = [&] (Win w, const char* srcName, const std::string& label) -> std::shared_ptr<Pad>
        {
            if (! w.ok()) return nullptr;
            const long s0 = beatSample (barBeat (w.j)), s1 = s0 + jsRound (w.nb * 4 * grid.T * SR);
            Stereo a = synth (srcName, s0, s1);
            if (a.L.empty()) return nullptr;
            fades (a, 1.5, 2);
            auto p = makePad (std::move (a), label + " " + std::to_string (w.nb) + " batt.", "battuta " + std::to_string (w.j + 1) + " \xc2\xb7 " + fmtT (s0 / SR), s0 / SR, "loop");
            if (p) { p->bars = w.nb; p->loop = true; }
            return p;
        };
        auto rel = [&] (const std::vector<float>& num, const std::vector<float>& den)
        {
            return [&, pnum = &num, pden = &den] (int j, int nb) { const double n = barSum (*pnum, j, nb), d = barSum (*pden, j, nb); return n * (n / (d + 1e-12)); };
        };
        Win wA = best (4, [&] (int j, int nb) { return barSum (bT, j, nb) + 0.5 * barSum (bP, j, nb); }, false);
        if (wA.ok()) ranges.push_back ({ wA.j, wA.j + wA.nb });
        Win wS = best (2, [&] (int j, int nb) { return barSum (bT, j, nb); }, true);
        if (wS.ok()) ranges.push_back ({ wS.j, wS.j + wS.nb });
        Win wD = best (1, [&] (int j, int nb) { return barSum (bT, j, nb); }, true);
        auto instScore = [&] (int j, int nb) { return barSum (bI, j, nb) + barSum (bP, j, nb) - 2 * barSum (bV, j, nb); };
        std::array<std::shared_ptr<Pad>, 8> out;
        out[0] = mk (wA, "mix", "Mix"); out[1] = mk (wS, "mix", "Mix"); out[2] = mk (wD, "mix", "Mix");
        out[3] = mk (best (2, rel (bP, bT), false), "drums", "Batteria");
        out[4] = mk (best (4, instScore, false), "inst", "Strumentale");
        out[5] = mk (best (4, rel (bV, bT), false), "vocals", "A cappella");
        out[6] = mk (best (2, rel (bI, bT), false), "chords", "Accordi");
        out[7] = mk (best (2, rel (bB, bT), false), "bass", "Basso");
        return out;
    }

    // ---------------------------------------------------------------- accordi
    std::array<std::shared_ptr<Pad>, 8> selectChords (const std::vector<float>& bI)
    {
        const int nb = grid.nBeats;
        std::vector<float> cb ((size_t) nb * 12, 0.f);
        for (int b = 0; b < nb; ++b)
        {
            const long f0 = std::max (0L, jsRound ((grid.t0 + b * grid.T) * fps)), f1 = jsRound ((grid.t0 + (b + 1) * grid.T) * fps);
            for (long f = f0; f < f1 && f < F; ++f) for (int p = 0; p < 12; ++p) cb[b * 12 + p] += feat.chroma[f * 12 + p];
        }
        struct Tpl { float v[12]; int r; bool minor; };
        std::vector<Tpl> tpl;
        for (int mi = 0; mi < 2; ++mi)
            for (int r = 0; r < 12; ++r)
            {
                Tpl t {}; t.r = r; t.minor = mi == 1;
                t.v[r] = 1; t.v[(r + (t.minor ? 3 : 4)) % 12] = 1; t.v[(r + 7) % 12] = 1;
                tpl.push_back (t);
            }
        const int k = keyInfo.root;
        std::set<int> dia = keyInfo.minor
            ? std::set<int> { k + 12, (k + 3) % 12, ((k + 5) % 12) + 12, ((k + 7) % 12) + 12, (k + 7) % 12, (k + 8) % 12, (k + 10) % 12 }
            : std::set<int> { k, ((k + 2) % 12) + 12, ((k + 4) % 12) + 12, (k + 5) % 12, (k + 7) % 12, ((k + 9) % 12) + 12 };
        std::vector<float> sc ((size_t) nb * 24, 0.f);
        for (int b = 0; b < nb; ++b)
        {
            double n = 0; for (int p = 0; p < 12; ++p) n += (double) cb[b * 12 + p] * cb[b * 12 + p];
            n = std::sqrt (n); if (n == 0) n = 1;
            for (int c = 0; c < 24; ++c)
            {
                double s = 0; for (int p = 0; p < 12; ++p) s += cb[b * 12 + p] * tpl[c].v[p];
                sc[b * 24 + c] = (float) (s / (n * std::sqrt (3.0)) + (dia.count (c) ? 0.04 : 0));
            }
        }
        std::vector<float> pos; for (float x : bI) if (x > 0) pos.push_back (x);
        const double med = percentile (pos, 0.5);
        std::vector<int> lab (nb, -1); std::vector<float> conf (nb, 0.f);
        for (int b = 0; b < nb; ++b)
        {
            if (bI[b] < 0.08 * med) { lab[b] = -1; continue; }
            int bc = 0; double bs = -1;
            for (int c = 0; c < 24; ++c)
            {
                const double s = 0.5 * (b > 0 ? sc[(b - 1) * 24 + c] : 0) + sc[b * 24 + c] + 0.5 * (b < nb - 1 ? sc[(b + 1) * 24 + c] : 0);
                if (s > bs) { bs = s; bc = c; }
            }
            lab[b] = bc; conf[b] = sc[b * 24 + bc];
        }
        struct Seg { int c, b0, n; double conf; };
        std::vector<Seg> segs;
        for (int b = 0; b < nb;)
        {
            int e = b; while (e < nb && lab[e] == lab[b]) e++;
            if (lab[b] >= 0) { double c = 0; for (int x = b; x < e; ++x) c += conf[x]; segs.push_back ({ lab[b], b, e - b, c / (e - b) }); }
            b = e;
        }
        progression.clear();
        for (int j = 0; j < grid.nBars; ++j)
        {
            std::map<int, int> cnt; std::vector<int> order;
            for (int b = barBeat (j); b < barBeat (j + 1); ++b)
                if (b >= 0 && b < nb && lab[b] >= 0) { if (! cnt.count (lab[b])) order.push_back (lab[b]); cnt[lab[b]]++; }
            // come Object.entries(...).sort(): chiavi intere in ordine crescente, poi ordinamento stabile per conteggio
            std::vector<std::pair<int, int>> e (cnt.begin(), cnt.end());
            std::stable_sort (e.begin(), e.end(), [] (auto& a, auto& b) { return a.second > b.second; });
            progression.push_back (e.empty() ? "-" : chordName (tpl[e[0].first].r, tpl[e[0].first].minor));
        }
        struct Agg { int c, first, total = 0; Seg best {}; double bestQ = -1; bool has = false; };
        std::map<int, Agg> byC; std::vector<int> insertion;
        for (auto& s : segs)
        {
            if (! byC.count (s.c)) { byC[s.c] = Agg { s.c, s.b0 }; insertion.push_back (s.c); }
            auto& o = byC[s.c]; o.total += s.n;
            const double q = std::min (s.n, 4) + s.conf;
            if (! o.has || q > o.bestQ) { o.best = s; o.bestQ = q; o.has = true; }
        }
        // Object.values: chiavi intere in ordine crescente
        std::vector<Agg> list;
        for (auto& kv : byC) if (kv.second.best.n >= 2 || kv.second.total >= 2) list.push_back (kv.second);
        std::stable_sort (list.begin(), list.end(), [] (const Agg& a, const Agg& b) { return a.total > b.total; });
        if (list.size() > 8) list.resize (8);
        std::stable_sort (list.begin(), list.end(), [] (const Agg& a, const Agg& b) { return a.first < b.first; });
        std::array<std::shared_ptr<Pad>, 8> out;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto& s = list[i].best;
            const int n = std::max (2, std::min (4, s.n));
            const long s0 = beatSample (s.b0), s1 = beatSample (s.b0 + n);
            Stereo a = synth ("harm", s0, s1);
            if (a.L.empty()) continue;
            fades (a, 4, 50);
            const std::string nm = chordName (tpl[list[i].c].r, tpl[list[i].c].minor);
            out[i] = makePad (std::move (a), nm, std::to_string (n) + " mov. \xc2\xb7 " + fmtT (s0 / SR), s0 / SR, "chord");
            if (out[i]) out[i]->chord = nm;
        }
        return out;
    }

    // ---------------------------------------------------------------- basso
    struct YinRes { double f0, clarity; bool ok; };
    static YinRes yin (const std::vector<float>& x, double sr)
    {
        const int tMin = (int) std::floor (sr / 400), tMax = (int) std::ceil (sr / 28);
        const long W = std::min ((long) x.size() - tMax - 1, (long) std::floor (sr * 0.12));
        if (W < tMax) return { 0, 0, false };
        std::vector<float> d (tMax + 1, 0.f), cm (tMax + 1, 0.f);
        for (int tau = 1; tau <= tMax; ++tau) { double s = 0; for (long j = 0; j < W; ++j) { const double v = x[j] - x[j + tau]; s += v * v; } d[tau] = (float) s; }
        cm[0] = 1; double run = 0;
        for (int tau = 1; tau <= tMax; ++tau) { run += d[tau]; cm[tau] = (float) (d[tau] * tau / (run != 0 ? run : 1)); }
        int tau = -1;
        for (int t = tMin; t <= tMax; ++t)
            if (cm[t] < 0.15f) { while (t + 1 <= tMax && cm[t + 1] < cm[t]) t++; tau = t; break; }
        if (tau < 0)
        {
            double bv = 1;
            for (int t = tMin; t <= tMax; ++t) if (cm[t] < bv) { bv = cm[t]; tau = t; }
            if (bv > 0.3 || tau < 0) return { 0, 0, false };
        }
        double tt = tau;
        if (tau > 1 && tau < tMax)
        {
            const double a = cm[tau - 1], b = cm[tau], c = cm[tau + 1], den = a - 2 * b + c;
            if (std::abs (den) > 1e-9) tt = tau + 0.5 * (a - c) / den;
        }
        return { sr / tt, 1 - cm[tau], true };
    }
    std::array<std::shared_ptr<Pad>, 8> selectBass()
    {
        auto on = peaks (feat.fluxBass, 4, (int) jsRound (fps * 0.5), 0.1);
        const int D = std::max (1, (int) std::floor (SR / 4000));
        const double sr2 = SR / D;
        struct C { int f; double t, gap, e = 0; };
        std::vector<C> cands;
        for (size_t i = 0; i < on.size(); ++i) cands.push_back ({ on[i], on[i] / fps, i + 1 < on.size() ? (on[i + 1] - on[i]) / fps : 1.0 });
        double eMax = 0;
        for (auto& c : cands) { double e = 0; for (int g = c.f; g < std::min (F, c.f + 8); ++g) e += feat.eBass[g]; c.e = e; eMax = std::max (eMax, e); }
        std::vector<C> top;
        for (auto& c : cands) if (c.e > 0.05 * eMax && c.gap >= 0.16) top.push_back (c);
        std::stable_sort (top.begin(), top.end(), [] (const C& a, const C& b) { return a.e > b.e; });
        if (top.size() > 60) top.resize (60);
        struct Note { int midi; double f0, q, t; long s0; Stereo a; };
        std::map<int, Note> notes;
        for (auto& c : top)
        {
            const double len = std::min (1.0, c.gap - 0.01);
            const long s0 = jsRound ((c.t - 0.01) * SR);
            Stereo a = synth ("bass", s0, s0 + jsRound (len * SR));
            if (a.L.empty()) continue;
            const long off = jsRound (0.03 * SR);
            const long n = std::max (0L, (long) std::floor ((double) ((long) a.L.size() - off) / D));
            std::vector<float> x (n);
            for (long i = 0; i < n; ++i) { double s = 0; for (int j = 0; j < D; ++j) { const long k = off + i * D + j; s += a.L[k] + a.R[k]; } x[i] = (float) (s / (2 * D)); }
            auto r = yin (x, sr2);
            if (! r.ok) continue;
            const int midi = (int) jsRound (69 + 12 * std::log2 (r.f0 / 440));
            if (midi < 23 || midi > 64) continue;
            const double q = r.clarity * std::sqrt (c.e / eMax) * std::min (1.0, len / 0.4);
            auto it = notes.find (midi);
            if (it == notes.end() || q > it->second.q) notes[midi] = Note { midi, r.f0, q, c.t, s0, std::move (a) };
        }
        // come nel JS: scorre una copia delle chiavi e cancella subito, cosi' le note gia' tolte non contano piu'
        std::vector<int> keys; for (auto& kv : notes) keys.push_back (kv.first);
        for (int m : keys)
        {
            auto self = notes.find (m);
            if (self == notes.end() || inKey (m)) continue;
            for (int nb : { m - 1, m + 1 })
            {
                auto it = notes.find (nb);
                if (it != notes.end() && inKey (nb) && it->second.q >= 0.25 * self->second.q) { notes.erase (self); break; }
            }
        }
        std::vector<Note*> list;
        for (auto& kv : notes) list.push_back (&kv.second);
        std::stable_sort (list.begin(), list.end(), [] (Note* a, Note* b) { return a->q > b->q; });
        if (list.size() > 8) list.resize (8);
        std::stable_sort (list.begin(), list.end(), [] (Note* a, Note* b) { return a->midi < b->midi; });
        const char* const* names = keyInfo.flats ? NAMES_F : NAMES_S;
        std::array<std::shared_ptr<Pad>, 8> out;
        for (size_t i = 0; i < list.size(); ++i)
        {
            auto* o = list[i];
            const std::string nm = std::string (names[o->midi % 12]) + std::to_string (o->midi / 12 - 1);
            Stereo a = std::move (o->a);
            fades (a, 2, 40);
            out[i] = makePad (std::move (a), "Basso " + nm, fmt1 (o->f0) + " Hz \xc2\xb7 " + fmtT (o->t), o->s0 / SR, "bass");
            if (out[i]) { out[i]->note = nm; out[i]->midi = o->midi; }
        }
        return out;
    }

    // ---------------------------------------------------------------- regia
    Result sliceAll()
    {
        progress (0.8, "Scelgo i campioni migliori");
        auto bT = beatAgg (feat.eTot), bP = beatAgg (feat.ePerc), bV = beatAgg (feat.eVoc), bB = beatAgg (feat.eBass), bI = beatAgg (feat.eInst);
        Result r;
        progress (0.82, "Ritaglio la batteria");
        r.banks[0] = { "drums", selectDrums() };
        progress (0.87, "Ritaglio le voci");
        r.banks[1] = { "vox", selectVox (bV, bT) };
        progress (0.91, "Preparo i loop");
        r.banks[2] = { "loops", selectLoops (bT, bP, bV, bB, bI) };
        progress (0.95, "Riconosco gli accordi");
        r.banks[3] = { "chords", selectChords (bI) };
        refineKey();
        progress (0.97, "Estraggo le note di basso");
        r.banks[4] = { "bass", selectBass() };
        r.sr = SR; r.dur = (double) LEN / SR; r.cappedSec = cappedSec; r.bpm = grid.bpm;
        r.beat0 = grid.t0 + grid.d * grid.T; r.T = grid.T; r.nBars = grid.nBars;
        r.key = keyInfo; r.progression = progression; r.fromStems = stems;
        progress (1.0, "Fatto");
        return r;
    }
};

// =====================================================================================
Engine::Engine() : impl (std::make_unique<Impl> (params)) {}
Engine::~Engine() = default;

Result Engine::analyze (Sources src, double sampleRate, double cappedSec, Progress progress, const std::atomic<bool>* cancel)
{
    auto& I = *impl;
    I.src = std::move (src);
    I.stems = I.src.stems;
    const Stereo& ref = I.stems ? I.src.drums : I.src.mix;
    if (ref.L.size() < (size_t) sampleRate) throw std::runtime_error ("Il brano e' troppo corto (serve almeno 1 secondo).");
    if (I.stems)
        for (auto* s : { &I.src.drums, &I.src.vocals, &I.src.bass, &I.src.other })
            if (s->L.size() != ref.L.size() || s->R.size() != ref.L.size()) throw std::runtime_error ("Gli stem devono avere la stessa lunghezza.");
    I.SR = sampleRate; I.LEN = (long) ref.L.size(); I.cappedSec = cappedSec;
    I.fps = I.SR / HOP; I.F = (int) std::ceil ((double) I.LEN / HOP) + 1;
    I.progressCb = std::move (progress); I.cancel = cancel;
    try
    {
        I.setupBins();
        I.progress (0.02, "Preparazione");
        if (! I.stems) { I.pass1(); I.buildMasks(); I.smoothMasks(); I.vocalGate(); }
        I.pass2();
        if (! I.stems) { std::vector<float>().swap (I.mag); std::vector<uint8_t>().swap (I.cw); }
        I.progress (0.75, "Tempo e tonalita'");
        I.detectKey();
        I.makeGrid (0);
        return I.sliceAll();
    }
    catch (const Cancelled&) { throw std::runtime_error ("Analisi annullata."); }
}

Result Engine::reslice (double bpm, Progress progress, const std::atomic<bool>* cancel)
{
    auto& I = *impl;
    if (I.F == 0) throw std::runtime_error ("Nessun brano analizzato.");
    I.progressCb = std::move (progress); I.cancel = cancel;
    try
    {
        I.progress (0.75, "Ricalcolo la griglia");
        I.detectKey();  // la tonalita' riparte dal profilo, come nel JS dopo un nuovo ritaglio
        I.makeGrid (bpm);
        return I.sliceAll();
    }
    catch (const Cancelled&) { throw std::runtime_error ("Operazione annullata."); }
}

Stereo Engine::synthFull (const std::string& name) const { return impl->synth (name, 0, impl->LEN); }
Stereo Engine::synthRange (const std::string& name, long s0, long s1) const { return impl->synth (name, s0, s1); }

}  // namespace sf
