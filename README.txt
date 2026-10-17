Indigo Magic (version 2.4.0)
============================

The IRIX 6.5 "Background Setting" panel from SGI's Indigo Magic desktop, recreated and
expanded for Windows 10/11 (64-bit). One .exe, no installer, no runtime.
Run IndigoMagic.exe.

  Indigo Magic was the name of the IRIX desktop.

WHICH VERSION AM I RUNNING?
  The version is shown at the top right of the panel ("Version 2.4.0"), in the About box
  (Help > About..., or the window-menu button at the top left > About Indigo Magic...),
  in the exe's Properties > Details in Explorer, and at the top of every crash report.

NEW IN 2.4
  * Smooth, antialiased generated art. Every generated pattern is now drawn at the size it is
    shown and supersampled, so edges, lines and curves are clean instead of jagged - at 1x
    and close up alike. Scale (2x, 3x, 4x) redraws a generated tile larger rather than
    blowing up its pixels. The IRIX artwork keeps its original pixels (Pixel Size).
  * New Transform tab: Scale / Pixel Size, Tile Size, Smoothing (Off / Normal / High),
    Fit, Balance (shift a pattern towards its dark or light colours), Invert Colours,
    Rotate (0/90/180/270), Mirror Left-Right / Top-Bottom, and Shift Across / Down (slide a
    tile out from under your icons). All saved in presets.
  * Pattern tab: room for up to six controls per pattern; the new patterns use it.
  * IRIX Classics is now exactly the list of the real IRIX Background Setting panel - the
    37 backgrounds from Brain to Wicker, Twilight included, with their names untouched.
  * Everything else is organised into families: Geometric, Wireframe, Fractals, Retro
    Raster, Decorative, Topographic, Abstract, Procedural Textures, plus Demoscene, Neon 90s
    and Text Friendly. The old "New Patterns" were spread into the families and are now
    drawn smoothly at any size too.
  * 38 new patterns:
      Geometric      - Interlocking Squares, Nested Diamonds, Beveled Tiles, Isometric Cubes,
                       Cairo Tiling
      Wireframe      - Wire Mesh, Perspective Grid, Moire Interference, Sine Grid,
                       Wire Polyhedra
      Fractals       - Mandelbrot Orbit Trap, Julia Garden, Pythagoras Tree, Whirling
                       Squares, Sierpinski Gasket, Stained Glass (Voronoi)
      Retro Raster   - Halftone, Bayer Dither, Stipple, Crosshatch, Line Screen
      Decorative     - Deco Arches, Deco Fans, Deco Sunrise, Rosette, Kaleidoscope
      Topographic    - Contour Lines, Elevation Bands, Ridgelines, Flow Lines
      Abstract       - Low-Poly Crystals, Honeycomb, Lava Blobs, Circuit Board
      Procedural     - Turbulence, Fractal Noise, Distorted Grid, Liquid Marble
  * The sgi wordmark on the IRIX backdrops (Citrus Citrus, Reef, Sky (HDTV) ...): SGI's
    own images contain only the top of the letters - on IRIX the wordmark ran off the
    bottom of the screen - so it always looked cut in half. It is now hidden by default;
    the backdrop's Pattern tab can show it again exactly as IRIX did.
  * "Octane Edition" is gone from the name.
  * Also: Moire Rings is now a full-screen pattern (no visible tile seams), Hilbert Curve
    no longer shows bands at the tile edges, Vignette is lighter in the centre as intended,
    and Boomerangs are smooth crescents.
  * Your settings, presets and palettes carry over; presets that used a retired pattern
    open its closest relative.

NEW IN 2.3.1
  * Fixed: in Theme Options, the Apply, Revert and Close buttons did nothing when clicked
    (only double-clicking a scheme or pressing Enter / Esc worked). The self test now
    clicks them with the mouse as well.

NEW IN 2.3
  * Theme Options: the program itself can now wear any of the IRIX 6.5 desktop colour
    schemes, taken straight from IRIX's own scheme files (/usr/lib/X11/schemes):
    Indigo Magic (the default), Arizona, Bayou, Black and White, Buckingham, Gainsborough,
    Gotham, Gray Scale, Inverness, Key West, Mendocino, Metropolis, Milan, Monet, Pacific,
    Potrero, Print, Red-Green Safe, Rio, Rose Garden, Sargent, Titian and Vancouver (23 -
    the 6.2-era names that were exact copies, like Lascaux, Whistler or Van Gogh, are the
    same scheme and still accepted).
      Open it with the Theme Options... button at the top right, Ctrl+T, or the window
    menu. Pick a scheme on the left to see a sample window in it, then Apply (or
    double-click). Revert goes back to the theme you started with. Your choice is saved.
      Every colour follows the scheme's own roles exactly as IRIX used them: window
    background, push buttons, the rose / green / teal text fields, scrolled lists (with
    Motif's reverse-video selection), scrollbars and sliders, the red check marks and
    blue diamonds of the toggles, the drawing area behind the preview, the active and
    inactive 4Dwm frame and title text, the Color Browser, dialogs, and the menus (now
    drawn in the scheme with IRIX's bold-oblique menu font). The 3-D shadows are derived
    from each widget's own colour the way SGI's Motif did. The values are the ones IRIX
    used for a PC-style display (GAMMA_1_0), which match real IRIX screenshots exactly.
  * IRIX Extras (20 new backgrounds): the desktop pixmaps IRIX shipped but never listed
    (Mid Granite, Swirl II, Twill) and the classic X11 root-window bitmaps from
    /usr/include/X11/bitmaps - Root Weave, Cross Weave, Wide Weave, Plaid, Boxes, Polka
    Dots, Open Dots, Stars, Targets, Tie Fighter, Wingdogs, Mens et Manus, X Snow,
    SGI Cube, X Logo, Graph Paper, Dimples.
  * Removed 16 backgrounds that did not work as wallpapers or did not look like their
    names: Burning Ship, Fourier Terrain, Interference, Metaballs, Kaleidoscope,
    Hypercube, Superformula, Spirograph, Lorenz Butterfly, Copper Bars, CRT Scanlines,
    Chrome Spheres, Iso Cubes, Scribble Hatch, Honeycomb and Static. A preset or exported
    .irixbg file that uses one opens its closest relative instead (Burning Ship ->
    Mandelbrot, Copper Bars -> Synth Grid, Honeycomb -> Diamond Lattice ...) with its
    colours kept.
  * Fixed: "Show: IRIX Classics" listed every background instead of just the classics.
  * Fixed: Make Text-Friendly could lower an already Excellent rating to Good.

NEW IN 2.2
  * The program is now called Indigo Magic (2.1 called it Indigo Magic Backgrounds); the
    exe is IndigoMagic.exe. Settings, presets, palettes and Keep Synced carry over - Keep
    Synced switches itself to the new exe the first time you run it.
  * Version information: shown in the panel, in a new About box, and in the exe properties.
  * Dialog buttons now widen to fit longer labels.

NEW IN 2.1.1
  * Every tiled pattern now starts at its own best-looking Pixel Size, picked one by one
    from side-by-side 1x / 2x / 3x / 4x renders. Small pixel-art tiles (Houndstooth, Moorish,
    Zellige, Scallop, Brain, Cellular Automaton ...) start at 2x; fine textures, stone and
    marble (Granite, Rockface, Verde Marble ...) and the smooth originals start at 1x.
  * On a high-scaling screen (a laptop at 150%, a 4K monitor at 150-200%) the pixel-art
    patterns start correspondingly larger, so they look the same size as on a 100% screen.
    The smooth originals stay 1x - for them Tile Size is the size control.
  * Fixed: patterns opening at 4x. Your v1 Pixel Size had been copied to every pattern when
    your v1 settings were brought over. Those patterns are put back to their own default
    once (a Pixel Size you set on just a few patterns is kept), and v1's setting is no
    longer copied. The background that is currently applied is not changed.
  * A Pixel Size you choose is still saved with that pattern and in presets.

NEW IN 2.1
  * New name: Indigo Magic Backgrounds (was "IRIX Background Setting - Silicon Dreams
    Edition"). The collections are now Fractals & Chaos (was Silicon Dreams) and
    Demoscene (was Early CGI); the Silicon Dreams palette group is now Fractal Glow.
    Your settings, presets and palettes carry over (same %APPDATA%\IrixBackground folder).
  * The minimize and maximize buttons on the title bar work. Maximize fills the screen
    above the taskbar; click it again to restore. Double-clicking the title bar also works.
  * Bugs fixed:
    - Maximizing used to cover the taskbar; it now fills the screen above it.
    - The Color Browser could keep editing the previous pattern after Undo / Redo switched
      patterns (and Cancel wrote the old colour back into it).
    - Signing out, restarting or shutting down with the window open lost that session's
      changes. They are now saved.
    - Closing during Apply or Export cut the task off half-written; it now finishes first
      (close again to quit at once).
    - Keep Synced re-applied this wallpaper on the next display change even after you had
      chosen another one in Windows Settings. It now leaves your new choice alone.
    - Keep Synced kept pointing at the old exe after a rename or move (2.1 renames the exe);
      it now follows the copy you run.
    - Settings could fail to save while antivirus, indexing or cloud sync briefly held the
      file.
    - Presets named CON, PRN, AUX, NUL, COM1-9 or LPT1-9 could not be saved.
    - Palette names containing "=" changed after a restart.
    - A huge custom export could keep about 1 GB of memory cached afterwards; caches now
      scale with installed memory. Running out of memory shows a plain message.
  * The window remembers its position, size and whether it was maximised.

NEW IN 2.0.1
  * Fixed: the program closed itself when its window was minimised - with the title-bar
    button, from the taskbar, or with Win+D / Show desktop. (Windows still asks a minimised
    self-drawn window to paint into a 160 x 31 area; the panel tried to lay itself out in it.)
  * Fits small and highly scaled screens: on a 14" 1080p laptop at 150-175% or 1366x768 at
    125% the panel used to be taller than the screen, hiding Apply / Close. It now scales
    itself just enough to fit, and keeps fitting when moved between monitors.
  * Crash reports: if anything ever goes wrong, details go to crash.log (see below) and the
    program keeps running wherever it can instead of disappearing.
  * Sturdier rendering of flat / degenerate fields (could read out of range at tiny sizes).

WHAT'S IN IT (200 backgrounds)
  * IRIX Classics (37)  - exactly the IRIX 6.5 list, factory colours and names.
  * IRIX Extras (20)    - unlisted IRIX pixmaps and the classic X11 root bitmaps.
  * 138 generated patterns, each with its own controls, in families:
      Geometric (11), Wireframe (10), Fractals (22), Retro Raster (9), Decorative (7),
      Topographic (10), Abstract (11), Procedural Textures (13), Demoscene (10),
      Neon 90s (15), Text Friendly (20 - calm textures that keep icon labels crisp,
      marked "Aa" in the list)
  * Gradients (5)       - smooth / banded / dithered 4-stop gradients.

THEMES
  Theme Options (top right, Ctrl+T, or the window menu) recolours the program with any of
  the 23 IRIX desktop schemes. See NEW IN 2.3.

BROWSING
  "Show" filters by collection (or Text-Friendly Picks); "Find" searches names and
  descriptions. Thumbnails always show your current colours.

COLORS TAB
  * Click a colour square for the Color Browser (HSV wheel, sliders, hex). Right-click a
    square to copy/paste hex, lock it or re-roll it.
  * Padlock = keep this colour when randomizing. Die = re-roll just this colour.
  * Randomize Palette (Ctrl+R) in a Harmony: Auto, Analogous, Complementary, Triadic,
    Split Complement, Tetradic, Monochrome, 90s Neon, SGI Workstation, Psychedelic,
    Pastel Dream, Midnight, Earth & Metal, Calm (Text Friendly). Shuffle swaps roles.
  * Palette Library: 12 x 90s Neon (Saturday Morning, Jazz Cup, Memphis Milano, Arcade
    Carpet, Windbreaker, Laser Tag, Slap Bracelet ...), SGI Classic, Fractal Glow,
    Text Friendly, Nature & Metal, plus My Palettes. Save / Delete / Import / Export .gpl.
  * Color cycling: Ramp Repeat, Ramp Offset, Mirror. Reverse Order, Factory Colors.

PATTERN TAB
  The pattern's own sliders / choices (up to six), Shading (Smooth, Banded, Dither, IRIX
  diffusion), New Variation (Ctrl+N) and Set Seed, Reset Pattern Controls. The IRIX
  backdrops have the sgi Wordmark choice here. Double-click a slider's label to reset it;
  the mouse wheel nudges.

TRANSFORM TAB
  Scale 1-4x (generated tiles, redrawn smoothly) or Pixel Size 1-4x (IRIX art, its own
  pixels enlarged) - each pattern starts at its own best fit for your display; Tile Size;
  Smoothing Off / Normal / High; Fit (IRIX backdrops); Balance and Invert Colours
  (gradient-style patterns); Rotate; Mirror; Shift Across / Down (tiles); Reset Transform.

ADJUST TAB - readable icons
  Brightness, Contrast, Saturation, Hue Shift, Soften, Calm (flattens fine detail).
  Icon Labels Off / Light / Dark + Strength protect label contrast.
  Make Text-Friendly finds the gentlest settings that make labels read "Good".
  Under the preview, "Show Desktop Icons" overlays sample icons and the Labels meter
  rates legibility live (Poor / Fair / Good / Excellent with the contrast ratio).

PRESETS & EXPORT TAB
  Save New (Ctrl+S), Update, Load (or double-click), Rename, Delete, Import / Export
  .irixbg files. A preset stores every setting and restores it exactly.
  Export Image: all displays, primary display, one seamless tile, 1080p, 1440p, 4K, 5K
  or a custom size - PNG or BMP.
  Keep Synced to Displays: re-applies at sign-in and whenever displays change.

SYNCED TILING (as in v1)
  Tiles form one continuous plane across every display, phase-locked to the primary
  display's top-left corner; Windows spans the bitmap 1:1. The preview is locked to the
  desktop too - drag the window and the tiles stay put.

EVERYTHING ELSE
  Undo / Redo (Ctrl+Z / Ctrl+Y) for every change, Reset ... (Factory or Previous),
  Help (F1), Enter = Apply, Esc = close. Your v1 colour settings are migrated. The window
  remembers where it was, its size and whether it was maximised.
  Settings, presets and palettes: %APPDATA%\IrixBackground

IF SOMETHING GOES WRONG
  Problems are written to %APPDATA%\IrixBackground\crash.log: what failed, on which thread,
  what it was doing, your recent actions, the display setup and the call stack with function
  names. If the program has to close it says so and offers to open the report; please send
  that file along. Settings that cannot be read are set aside as state-unreadable.txt and the
  program starts with defaults rather than failing to open.

COMMAND LINE
  IndigoMagic.exe --apply      re-apply the last applied background
  IndigoMagic.exe --watch      the display-change helper
  IndigoMagic.exe --crash-test=render   checks crash reporting (writes a sample crash.log)
  IndigoMagic.exe --selftest log.txt
      developer check: drives every control on every pattern, the preset, palette, export,
      undo and apply workflows, minimise / maximise / restore, and the layout at every UI
      scale from 75% to 300% (your settings are backed up and restored, and your
      wallpaper is re-applied afterwards). It ends with thousands of random clicks, drags,
      key presses and window changes; add --monkey to run only those (IRIXBG_MONKEY=steps).

BUILDING (source folder)
  Linux/WSL: sudo apt install mingw-w64 && ./build.sh   (./build.sh tests = engine + fuzz tests)
  Windows:   MSYS2 MinGW-w64 g++ on PATH, run build.bat

SmartScreen may warn the first time (the exe isn't code-signed): More info -> Run anyway.
