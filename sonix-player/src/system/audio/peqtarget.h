#ifndef PEQTARGET_H
#define PEQTARGET_H

#include <stdbool.h>
#include <stddef.h>

// AutoEq publishes its profiles for one target (Harman). This moves such a
// profile to another target: the curve the downloaded filters draw, plus the
// difference between the two targets, fitted back into ten filters.
//
// A target is a number from 0 (Harman, the profile as downloaded) to
// peqtarget_count() - 1; the list is in peqtarget.c.
typedef enum { PEQTARGET_OVER_EAR = 0, PEQTARGET_IN_EAR = 1 } peqtarget_form_t;

#define PEQTARGET_HARMAN 0

int peqtarget_count(void);
const char *peqtarget_name(int target);	 // as shown in the list
const char *peqtarget_group(int target); // heading it is listed under
const char *peqtarget_tag(int target);	 // short, for the preset's name
// A target made for one kind of headphone has no curve for the other kind.
bool peqtarget_fits(int target, peqtarget_form_t form);

int peqtarget_get(void); // saved in config [peq] autoeq_target
void peqtarget_set(int target);

// Rewrites an Equalizer APO / AutoEq ParametricEQ text for `target`. Returns a
// malloc'd text (caller frees) or NULL when the input has no usable filter or
// the target has no curve for this kind of headphone.
char *peqtarget_retarget(const char *text, size_t size, int target, peqtarget_form_t form, size_t *out_size);

#endif
