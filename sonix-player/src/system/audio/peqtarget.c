#include "src/system/audio/peqtarget.h"

#include "src/system/core/config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// What AutoEq fits its published profiles to is not one curve: in-ears get
// its own "AutoEq in-ear" target with an 8 dB bass shelf, over-ears Harman
// over-ear 2018 (dbtools/create_webapp_data.py in its repository, checked
// against the target column of its result files). The tables hold each other
// target minus that one, per kind of headphone; tools/peqtarget_tables.py
// writes them and says where the curves come from.
#define NBANDS 10
#define NPTS 121 // 1/12 octave, 20 Hz .. 20.5 kHz
#define FS 48000.0

enum { T_PK = 0, T_LS, T_HS };

double peqtarget_last_rms; // shape error of the last fit, dB (for the log)

typedef struct {
	int type;
	double fc, gain, q;
} flt_t;

#include "src/system/audio/peqtarget_tables.h"

// The curves PEQdB lists in its studio, under its own headings. The first one
// is no curve at all: the profile as AutoEq made it. Harman over-ear 2018 is
// not in the list because that is what the first one already is for over-ears.
// The ids are what the config file holds, so they stay as they are.
static const struct {
	const char *id, *name, *tag, *group;
	const float *ie, *oe;
} targets[] = {
	{"harman", "AutoEq default", "", "AutoEq", NULL, NULL},
	{"diamond", "PEQdB Diamond \xCE\xB2", "Diamond", "Reference", delta_diamond_ie, delta_diamond_oe},
	{"diamond_a", "PEQdB Diamond", "Diamond A", "Reference", delta_diamond_a_ie, delta_diamond_a_oe},
	{"df", "Diffuse Field", "DF", "Baseline", delta_df_ie, delta_df_oe},
	{"ultra", "PEQdB Ultra", "Ultra", "Preference", delta_ultra_ie, delta_ultra_oe},
	{"hfe", "HiFiEndgame", "HFE", "Preference", delta_hfe_ie, delta_hfe_oe},
	{"hfe2", "HiFiEndgame MKII", "HFE MKII", "Preference", delta_hfe2_ie, delta_hfe2_oe},
	{"hfe3", "HiFiEndgame MKIII", "HFE MKIII", "Preference", delta_hfe3_ie, delta_hfe3_oe},
	{"hfe5", "HiFiEndgame MKV", "HFE MKV", "Preference", delta_hfe5_ie, delta_hfe5_oe},
	{"moa", "Harman MoA 2025", "MoA", "Preference", delta_moa_ie, delta_moa_oe},
	{"harman_ie", "Harman IE 2019", "Harman IE", "Preference (in-ear)", delta_harman_ie_ie, NULL},
	{"knowles", "Knowles", "Knowles", "Preference (in-ear)", delta_knowles_ie, NULL},
	{"ief2025", "IEF Preference 2025", "IEF 2025", "Preference (in-ear)", delta_ief2025_ie, NULL},
	{"jm1", "JM-1 -10dB Tilt", "JM-1", "Preference (in-ear)", delta_jm1_ie, NULL},
	{"vdsf", "Moondrop VDSF", "VDSF", "Preference (in-ear)", delta_vdsf_ie, NULL},
	{"oe2018lb", "Harman OE 2018 Linear Bass", "Linear Bass", "Preference (over-ear)", NULL, delta_oe2018lb_oe},
	{"oe2013", "Harman OE 2013", "OE 2013", "Preference (over-ear)", NULL, delta_oe2013_oe},
	{"kb50", "KEMAR KB50xx -10dB Tilt", "KB50xx", "Preference (over-ear)", NULL, delta_kb50_oe},
	{"endgame_ie", "Endgame IE", "Endgame IE", "Legacy", delta_endgame_ie_ie, NULL},
	{"hifi", "PEQdB HiFi", "HiFi", "Legacy", delta_hifi_ie, delta_hifi_oe},
};
#define NTARGETS ((int)(sizeof(targets) / sizeof(targets[0])))

static bool known(int t) { return t >= 0 && t < NTARGETS; }

int peqtarget_count(void) { return NTARGETS; }
const char *peqtarget_name(int t) { return known(t) ? targets[t].name : ""; }
const char *peqtarget_group(int t) { return known(t) ? targets[t].group : ""; }
const char *peqtarget_tag(int t) { return known(t) ? targets[t].tag : ""; }

bool peqtarget_fits(int t, peqtarget_form_t form) {
	return known(t) && (form == PEQTARGET_IN_EAR ? targets[t].ie : targets[t].oe) != NULL;
}

int peqtarget_get(void) {
	const char *v = config_get("peq", "autoeq_target", "harman");
	for (int t = 0; v && t < NTARGETS; t++) {
		if (strcasecmp(v, targets[t].id) == 0) return t;
	}
	return PEQTARGET_HARMAN;
}

void peqtarget_set(int t) { config_set("peq", "autoeq_target", targets[known(t) ? t : 0].id); }

// RBJ cookbook magnitude in dB.
static double flt_db(const flt_t *f, double freq) {
	double A = pow(10.0, f->gain / 40.0);
	double w0 = 2 * M_PI * f->fc / FS;
	double cw = cos(w0), sw = sin(w0);
	double alpha = sw / (2 * f->q);
	double b0, b1, b2, a0, a1, a2;
	if (f->type == T_PK) {
		b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
		a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
	} else {
		double s = 2 * sqrt(A) * alpha;
		if (f->type == T_LS) {
			b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
			a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s;
		} else {
			b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
			a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
		}
	}
	double w = 2 * M_PI * freq / FS;
	double c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w);
	double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
	double dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
	return 10.0 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

static double grid_f(int i) { return 20.0 * pow(2.0, i / 12.0); }

static double delta_at(const float *tab, int i) {
	int k = i / 2;
	if (k >= 60) return tab[60];
	return (i & 1) ? 0.5 * (tab[k] + tab[k + 1]) : tab[k];
}

static double weight(double f) { return f > 10000 ? 0.3 : (f < 30 ? 0.5 : 1.0); }

static int parse(const char *text, size_t size, flt_t *out) {
	int n = 0;
	const char *p = text, *end = text + size;
	while (p < end && n < NBANDS) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
		char line[256];
		if (len >= sizeof(line)) len = sizeof(line) - 1;
		memcpy(line, p, len);
		line[len] = 0;
		p = nl ? nl + 1 : end;
		char onoff[8], type[8];
		double fc, gain, q;
		const char *s = strstr(line, "Filter");
		if (!s) continue;
		s = strchr(s, ':');
		if (!s) continue;
		if (sscanf(s + 1, " %7s %7s Fc %lf Hz Gain %lf dB Q %lf", onoff, type, &fc, &gain, &q) != 5) continue;
		if (strcasecmp(onoff, "ON") != 0) continue;
		int t;
		if (strcasecmp(type, "PK") == 0 || strcasecmp(type, "PEQ") == 0) t = T_PK;
		else if (strncasecmp(type, "LS", 2) == 0) t = T_LS;
		else if (strncasecmp(type, "HS", 2) == 0) t = T_HS;
		else continue;
		if (fc < 10 || fc > 22000 || q <= 0) continue;
		out[n].type = t; out[n].fc = fc; out[n].gain = gain; out[n].q = q;
		n++;
	}
	return n;
}

// The fit moves only the shelves (and a peak turned into one when a shelf is
// missing): the difference between the targets is a bass and a treble tilt,
// and the peaks AutoEq placed on this headphone's own resonances stay where
// they are. Bounds are soft -- a penalty, not a clamp -- so the gradient never
// goes flat against a wall.
#define NPEN 3

static void unpack(const double *x, const int *idx, int m, flt_t *f) {
	for (int k = 0; k < m; k++) {
		flt_t *b = &f[idx[k] / 3];
		switch (idx[k] % 3) {
		case 0: b->fc = exp(x[k]); break;
		case 1: b->gain = x[k]; break;
		default: b->q = exp(x[k]); break;
		}
	}
}

static double over(double v, double lo, double hi) { return v < lo ? lo - v : (v > hi ? v - hi : 0); }

static double cost(const double *x, const int *idx, int m, flt_t *f, int n, const double *want, double *res) {
	unpack(x, idx, m, f);
	double sum = 0, mean = 0, wsum = 0;
	for (int j = 0; j < NPTS; j++) {
		double fr = grid_f(j), v = 0;
		for (int i = 0; i < n; i++) v += flt_db(&f[i], fr);
		res[j] = v - want[j];
		mean += weight(fr) * res[j];
		wsum += weight(fr);
	}
	mean /= wsum; // the level is the preamp's business, only the shape counts
	for (int j = 0; j < NPTS; j++) {
		res[j] = (res[j] - mean) * sqrt(weight(grid_f(j)));
		sum += res[j] * res[j];
	}
	double pen[NPEN] = {0, 0, 0};
	for (int i = 0; i < n; i++) {
		pen[0] += over(log(f[i].fc), log(20.0), log(16000.0));
		pen[1] += over(f[i].gain, -12.0, 12.0);
		pen[2] += over(log(f[i].q), log(0.3), log(4.0));
	}
	for (int k = 0; k < NPEN; k++) {
		res[NPTS + k] = 10.0 * pen[k];
		sum += res[NPTS + k] * res[NPTS + k];
	}
	return sum;
}

static int solve(double *a, double *b, int m) {
	for (int c = 0; c < m; c++) {
		int piv = c;
		for (int r = c + 1; r < m; r++) if (fabs(a[r * m + c]) > fabs(a[piv * m + c])) piv = r;
		if (fabs(a[piv * m + c]) < 1e-12) return 0;
		if (piv != c) {
			for (int k = 0; k < m; k++) { double t = a[c * m + k]; a[c * m + k] = a[piv * m + k]; a[piv * m + k] = t; }
			double t = b[c]; b[c] = b[piv]; b[piv] = t;
		}
		for (int r = c + 1; r < m; r++) {
			double fct = a[r * m + c] / a[c * m + c];
			for (int k = c; k < m; k++) a[r * m + k] -= fct * a[c * m + k];
			b[r] -= fct * b[c];
		}
	}
	for (int c = m - 1; c >= 0; c--) {
		double s = b[c];
		for (int k = c + 1; k < m; k++) s -= a[c * m + k] * b[k];
		b[c] = s / a[c * m + c];
	}
	return 1;
}

#define NRES (NPTS + NPEN)
#define MAXP (3 * NBANDS)

// Levenberg-Marquardt over the free parameters listed in idx.
static double fit(flt_t *f, int n, const int *idx, int m, const double *want, int rounds) {
	double x[MAXP], xt[MAXP], res[NRES], rt[NRES], A[MAXP * MAXP], g[MAXP];
	static double J[NRES][MAXP];
	flt_t tmp[NBANDS];
	memcpy(tmp, f, sizeof(flt_t) * n);
	for (int k = 0; k < m; k++) {
		const flt_t *b = &f[idx[k] / 3];
		x[k] = idx[k] % 3 == 0 ? log(b->fc) : (idx[k] % 3 == 1 ? b->gain : log(b->q));
	}
	double lambda = 1e-2;
	double c = cost(x, idx, m, tmp, n, want, res);
	for (int it = 0; it < rounds; it++) {
		for (int k = 0; k < m; k++) {
			memcpy(xt, x, sizeof(double) * m);
			double h = (idx[k] % 3 == 1) ? 1e-3 : 1e-4;
			xt[k] += h;
			cost(xt, idx, m, tmp, n, want, rt);
			for (int j = 0; j < NRES; j++) J[j][k] = (rt[j] - res[j]) / h;
		}
		bool improved = false;
		for (int tries = 0; tries < 10 && !improved; tries++) {
			for (int a = 0; a < m; a++) {
				g[a] = 0;
				for (int j = 0; j < NRES; j++) g[a] -= J[j][a] * res[j];
				for (int b = 0; b < m; b++) {
					double s = 0;
					for (int j = 0; j < NRES; j++) s += J[j][a] * J[j][b];
					A[a * m + b] = s + (a == b ? lambda * (s + 1e-6) : 0);
				}
			}
			if (!solve(A, g, m)) { lambda *= 10; continue; }
			for (int k = 0; k < m; k++) xt[k] = x[k] + g[k];
			double ct = cost(xt, idx, m, tmp, n, want, rt);
			if (ct < c) {
				bool done = c - ct < 1e-7 * c;
				memcpy(x, xt, sizeof(double) * m);
				memcpy(res, rt, sizeof(res));
				c = ct;
				lambda = lambda > 1e-7 ? lambda * 0.3 : lambda;
				improved = true;
				if (done) it = 1000;
			} else {
				lambda *= 10;
			}
		}
		if (!improved) break;
	}
	unpack(x, idx, m, f);
	return c;
}

char *peqtarget_retarget(const char *text, size_t size, int target, peqtarget_form_t form, size_t *out_size) {
	if (!peqtarget_fits(target, form)) return NULL;
	flt_t f[NBANDS];
	int n = parse(text, size, f);
	if (n == 0) return NULL;

	const float *tab = form == PEQTARGET_IN_EAR ? targets[target].ie : targets[target].oe;
	double want[NPTS];
	for (int j = 0; j < NPTS; j++) {
		double v = 0;
		for (int i = 0; i < n; i++) v += flt_db(&f[i], grid_f(j));
		want[j] = v + delta_at(tab, j);
	}
	// One low shelf and one high shelf to carry the difference: the ones the
	// profile has, or new ones in a free slot, or in place of its weakest peak.
	int ls = -1, hs = -1;
	for (int i = 0; i < n; i++) {
		if (f[i].type == T_LS && ls < 0) ls = i;
		if (f[i].type == T_HS && hs < 0) hs = i;
	}
	for (int want_type = T_LS; want_type <= T_HS; want_type++) {
		int *slot = want_type == T_LS ? &ls : &hs;
		if (*slot >= 0) continue;
		if (n < NBANDS) {
			*slot = n++;
		} else {
			int weakest = -1;
			for (int i = 0; i < n; i++)
				if (f[i].type == T_PK && (weakest < 0 || fabs(f[i].gain) < fabs(f[weakest].gain))) weakest = i;
			if (weakest < 0) return NULL;
			*slot = weakest;
		}
		f[*slot].type = want_type;
		f[*slot].fc = want_type == T_LS ? 105 : 8000;
		f[*slot].gain = 0;
		f[*slot].q = 0.7;
	}
	int idx[MAXP], m = 0;
	for (int k = 0; k < 3; k++) idx[m++] = ls * 3 + k;
	for (int k = 0; k < 3; k++) idx[m++] = hs * 3 + k;
	peqtarget_last_rms = sqrt(fit(f, n, idx, m, want, 100) / NPTS);
	// Two shelves carry a bass and a treble tilt, which is all Diamond asks
	// for. A target that also differs through the midrange or the ear-gain
	// region leaves a remainder; then every filter may move, starting from
	// where the shelves got to, with a spare peak where the ear gain sits
	// if the profile left a slot free.
	if (peqtarget_last_rms > 0.4) {
		if (n < NBANDS) {
			f[n].type = T_PK; f[n].fc = 3000; f[n].gain = 0; f[n].q = 1.0;
			n++;
		}
		m = 0;
		for (int i = 0; i < n; i++)
			for (int k = 0; k < 3; k++) idx[m++] = i * 3 + k;
		// (thirty parameters on this CPU: a few rounds get nearly all of it)
		peqtarget_last_rms = sqrt(fit(f, n, idx, m, want, 20) / NPTS);
	}

	// Preamp: the highest point of the new curve, taken off.
	double peak = 0;
	for (int j = 0; j < NPTS; j++) {
		double v = 0;
		for (int i = 0; i < n; i++) v += flt_db(&f[i], grid_f(j));
		if (v > peak) peak = v;
	}
	size_t cap = 128 + (size_t)n * 96;
	char *out = malloc(cap);
	if (!out) return NULL;
	size_t len = (size_t)snprintf(out, cap, "Preamp: %.1f dB\n", -peak);
	static const char *names[] = {"PK", "LSC", "HSC"};
	for (int i = 0; i < n; i++) {
		len += (size_t)snprintf(out + len, cap - len, "Filter %d: ON %s Fc %d Hz Gain %.1f dB Q %.2f\n", i + 1,
								names[f[i].type], (int)lround(f[i].fc), f[i].gain, f[i].q);
	}
	if (out_size) *out_size = len;
	return out;
}
