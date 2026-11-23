#ifndef SPLASH_H
#define SPLASH_H

// The picture between the boot menu and the first page: until now the screen
// stayed black while Sonix mounted the card, opened the library and built its
// pages. It is drawn on the top layer as soon as the display exists, in the
// colours of the saved theme or skin, and filled in step by step with
// lv_refr_now() -- the main loop is not running yet, so nothing animates on
// its own. splash_finish() fades it out over the first page.

void splash_show(void);

// 0..100: how far the start has come. Redraws at once.
void splash_step(int percent);

// Fades out and deletes the splash, once it has been up for a moment.
void splash_finish(void);

#endif /* SPLASH_H */
