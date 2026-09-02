#pragma once

#include "text_layout.hpp"

// Read-only font metrics: these functions do not access the drawing canvas.
text_layout_profile ui_reader_text_layout();
text_layout_profile ui_file_name_text_layout();
