# JPEG parser fixes for ESP-IDF 5.3.4

This local component overrides the installed driver only for this firmware.
Base files are from the installed ESP-IDF 5.3.4. `jpeg_decode.c`,
`jpeg_parse_marker.c` and `jpeg_private.h` contain Espressif's bounds checks from
release/v5.3 commit `313f55112eb6f744a2c5f509290ce189e67bdbf1`:
https://github.com/espressif/esp-idf/tree/313f55112eb6f744a2c5f509290ce189e67bdbf1/components/esp_driver_jpeg

The newer timeout dequeue change is excluded because the installed DMA pool
does not provide that API. The existing 5.3.4 timeout recovery is retained.
The startup regression check rejects a truncated APP segment and an invalid
quantization table index, then checks heap integrity before accepting USB frames.

Copyright and Apache-2.0 notices are retained. See ../../ESPRESSIF-LICENSE.
