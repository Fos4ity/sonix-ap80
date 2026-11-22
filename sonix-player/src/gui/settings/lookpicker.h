#ifndef LOOKPICKER_H
#define LOOKPICKER_H

// Settings -> Appearance: the lists a tap on "Font" and "Icons" opens. Every
// choice is a row drawn the way it will look -- a font's name in that font,
// an icon pack as a strip of its icons -- and a tap applies it at once.
// `on_done` runs after the list has closed, whether or not something changed.

void lookpicker_fonts(const char *title, void (*on_done)(void));
void lookpicker_icons(const char *title, void (*on_done)(void));

#endif /* LOOKPICKER_H */
