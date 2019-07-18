# atprogram-gui
A simple GUI wrapper around Atmel's atprogram utility with a very creative name...

# Intent
To allow inexperiecned users to easily program Atmel MCUs in a production environment. \
The GUI allows the flash process to be easily documented and reproduced.

# Features
* Drag & Drop multiple files at once
* Installs with atbackend and atpackmanager (optional)
* Searches for previously installed versions of atbackend and atpackmanager (Atmel Studio)
* Uses atpackmanager to determine and list supported MCUs
* Parses production files to determine which sections are available to be flashed
* Generates a CRC32 checksum of ELF files that can be used as a final verification step
* Clearly displays the outcome of the flash process (Pass / Fail)
* Easily determine the cause of errors by showing debug info from atprogram

# Screenshots

![Production File](https://user-images.githubusercontent.com/37219631/61396684-3c324900-a896-11e9-957f-49ccfaa86030.jpg)
![Memories](https://user-images.githubusercontent.com/37219631/61396679-39375880-a896-11e9-9439-e56e7a815f18.jpg)

![Error](https://user-images.githubusercontent.com/37219631/61401134-c92dd000-a89f-11e9-91a3-3e6deda15afd.jpg)
![Drag & Drop](https://user-images.githubusercontent.com/37219631/61401246-d945af80-a89f-11e9-977d-7e131ee90b02.gif)
