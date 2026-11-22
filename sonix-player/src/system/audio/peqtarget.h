#ifndef PEQTARGET_H
#define PEQTARGET_H

#include <stdbool.h>
#include <stddef.h>

// AutoEq publishes its profiles for one target (Harman). This moves such a
// profile to another target: the curve the downloaded filters draw, plus the
// difference between the two targets, fitted back into ten filters.
typedef enum { PEQTARGET_HARMAN = 0, PEQTARGET_DIAMOND = 1 } peqtarget_t;
typedef enum { PEQTARGET_OVER_EAR = 0, PEQTARGET_IN_EAR = 1 } peqtarget_form_t;

peqtarget_t peqtarget_get(void); // saved in config [peq] autoeq_target
void peqtarget_set(peqtarget_t target);

// Rewrites an Equalizer APO / AutoEq ParametricEQ text for `target`. Returns a
// malloc'd text (caller frees) or NULL when the input has no usable filter.
char *peqtarget_retarget(const char *text, size_t size, peqtarget_form_t form, size_t *out_size);

#endif
