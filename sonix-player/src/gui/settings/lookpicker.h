#ifndef LOOKPICKER_H
#define LOOKPICKER_H

// Settings -> Appearance: the lists a tap on "Font" and "Icons" opens. Every
// choice is a row drawn the way it will look -- a font's name in that font,
// an icon pack as a strip of its icons -- and a tap applies it at once.
// `on_done` runs after the list has closed, whether or not something changed.

void lookpicker_fonts(const char *title, void (*on_done)(void));
void lookpicker_icons(const char *title, void (*on_done)(void));

// "Theme": every look the whole interface can take, in one list -- Sonix's own
// dark and light, the colour themes and the Compas skins from the card -- each
// row painted in its own colours. A tap applies it at once.
void lookpicker_themes(const char *title, void (*on_done)(void));
// The theme before (-1) or after (+1) the one in force, in that list's order,
// going round at the ends: for arrows that step through them without the list.
void lookpicker_theme_step(int dir);
// The name of the theme in force, as that list shows it.
const char *lookpicker_theme_name(void);

#endif /* LOOKPICKER_H */
