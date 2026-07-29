#pragma once

// Active tool — mirrors Figma's toolbar set, scoped to what makes sense
// for a Mac OS 9 Carbon prototyping app.
enum class Tool {
    Select,     // V — select / move / resize objects
    Frame,      // F — create an artboard (screen)
    Rectangle,  // R — draw rectangle shape
    Ellipse,    // O — draw ellipse shape
    Text,       // T — place text node
    Hand,       // H — pan the canvas
    // Pen (bezier) deferred to a later milestone
};
