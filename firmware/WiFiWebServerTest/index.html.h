#include "styles.css.h"

String index_html = R"=====(
<!DOCTYPE html>
<html lang="es">
    <head>
        <title>WiFi Web Server Test</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <meta charset="utf-8">
        <style>
            )=====" + styles_css + R"=====(
        </style>
    </head>
    <body>
        <h1>BomberCat Web Server Test</h1>
        <button><a href="/on">ON</a></button>
    </body>
</html>
)=====";
