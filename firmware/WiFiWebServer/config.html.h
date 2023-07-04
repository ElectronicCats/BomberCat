const char* config_html = R"=====(<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Network</title>
    <link rel="shortcut icon" href="./Símbolo EC.svg" type="image/x-icon">
    <link rel="stylesheet" href="styles.css">
</head>

<body id="configPage">
    <div class="btn-back-container">
        <a href="./home.html" class="btn-back">
            <?xml version="1.0" encoding="utf-8"?><!-- Uploaded to: SVG Repo, www.svgrepo.com, Generator: SVG Repo Mixer Tools -->
            <svg width="40px" height="40px" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                <path d="M11 6L5 12M5 12L11 18M5 12H19" stroke="#000000" stroke-width="2" stroke-linecap="round"
                    stroke-linejoin="round" />
            </svg>
        </a>
    </div>
    <div class="main-background">
        <div class="main-content">
            <section class="ssid">
                <div class="form-container">
                    <h2 class="title">Configure Network</h2>
                    <p class="subtitle">Enter device access point name and password.</p>
                    <form action="#" class="form" method="POST">
                        <input type="text" required id="tvSSID" placeholder="Name" class="input"
                            name="ssid">
                        <input type="text" required id="tvPassword" placeholder="Password" class="input"
                            name="pass">
                        <input type="submit" id="btnSaveWiFiConfig" value="Save" class="primary-button">
                    </form>
                </div>
            </section>
        </div>
    </div>
    <footer id="footerSection">
        <div class="footer-home">
            <a href="./home.html" class="footer-icon-link">
                <div class="footer-icon">
                    <?xml version="1.0" encoding="utf-8"?>
                    <svg width="30px" height="30px" viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
                        <path fill="#1A5157" d="M512 128 128 447.936V896h255.936V640H640v256h255.936V447.936z" />
                    </svg>
                </div>
                <p class="footer-icon-text selected">Home</p>
            </a>
            <a href="./info.html" class="footer-icon-link">
                <div class="footer-icon">
                    <?xml version="1.0" encoding="utf-8"?><!-- Uploaded to: SVG Repo, www.svgrepo.com, Generator: SVG Repo Mixer Tools -->
                    <svg fill="#5f5f5f" height="35px" width="35px" id="Layer_1" data-name="Layer 1"
                        xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
                        <path class="cls-1"
                            d="M8,2a6,6,0,1,0,6,6A6,6,0,0,0,8,2ZM7.30274,4.28083a.95128.95128,0,0,1,1.34971,0,.92836.92836,0,0,1,.27615.67443.95244.95244,0,0,1-1.90483,0A.92362.92362,0,0,1,7.30274,4.28083ZM9.70043,12H6.29957V10.736h.83322V7.7772H6.29957V6.51325H8.88421V10.736h.81622Z" />
                    </svg>
                </div>
                <p class="footer-icon-text">Info</p>
            </a>
            <a href="" id="btnStore">
                <div class="footer-icon">
                    <?xml version="1.0" encoding="utf-8"?><!-- Uploaded to: SVG Repo, www.svgrepo.com, Generator: SVG Repo Mixer Tools -->
                    <svg fill="#5f5f5f" width="35px" height="35px" viewBox="0 0 24 24"
                        xmlns="http://www.w3.org/2000/svg">
                        <path
                            d="M22 5c0-1.654-1.346-3-3-3H5C3.346 2 2 3.346 2 5v2.831c0 1.053.382 2.01 1 2.746V19c0 1.103.897 2 2 2h14c1.103 0 2-.897 2-2v-8.424c.618-.735 1-1.692 1-2.746V5zm-2 0v2.831c0 1.14-.849 2.112-1.891 2.167L18 10c-1.103 0-2-.897-2-2V4h3c.552 0 1 .449 1 1zM10 4h4v4c0 1.103-.897 2-2 2s-2-.897-2-2V4zM4 5c0-.551.448-1 1-1h3v4c0 1.103-.897 2-2 2l-.109-.003C4.849 9.943 4 8.971 4 7.831V5zm6 14v-3h4v3h-4zm6 0v-3c0-1.103-.897-2-2-2h-4c-1.103 0-2 .897-2 2v3H5v-7.131c.254.067.517.111.787.125A3.988 3.988 0 0 0 9 10.643c.733.832 1.807 1.357 3 1.357s2.267-.525 3-1.357a3.988 3.988 0 0 0 3.213 1.351c.271-.014.533-.058.787-.125V19h-3z" />
                    </svg>
                </div>
                <p class="footer-icon-text store-text">Store</p>
            </a>
        </div>
    </footer>
    <script src="./main.js"></script>
</body>

</html>)=====";