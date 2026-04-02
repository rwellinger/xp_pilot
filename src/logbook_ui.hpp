#pragma once

// logbook_ui — Dear ImGui logbook window inside a XPLMCreateWindowEx window.
// Opens/closes via menu or keyboard command.

namespace LogbookUI {
    void init();   // call after FlightLogger::init()
    void stop();
    void open();
    void toggle();
    void draw();   // call every frame from main draw callback
}
