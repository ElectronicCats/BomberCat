const char* styles_css = R"=====(:root {
    --main-logo-background: #1A5157;
    --text-section: #333333;
    --fill-input-field: #F8F9FC;
    --seconday-color: #1dd6a5;
    --border: #aeaeae;
    --white: #FFFFFF;
    --text: #000000;
    --gray-text: #5f5f5f;
    --lg: 28px;
    --md: 16px;
    --sm: 12px;
    --xsm: 10px;
}

body {
    margin: 0;
    padding: 0;
    font-family: 'Segoe UI';
}

/* Animations */
.lds-roller {
    display: inline-block;
    position: relative;
    width: 160px;
    height: 160px;
}

.lds-roller div {
    animation: lds-roller 1.2s cubic-bezier(0.5, 0, 0.5, 1) infinite;
    transform-origin: 80px 80px;
}

.lds-roller div:after {
    content: " ";
    display: block;
    position: absolute;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: #53dda1;
    margin: -8px 0 0 -8px;
}

.lds-roller div:nth-child(1) {
    animation-delay: -0.036s;
}

.lds-roller div:nth-child(1):after {
    top: 126px;
    left: 126px;
}

.lds-roller div:nth-child(2) {
    animation-delay: -0.072s;
}

.lds-roller div:nth-child(2):after {
    top: 136px;
    left: 112px;
}

.lds-roller div:nth-child(3) {
    animation-delay: -0.108s;
}

.lds-roller div:nth-child(3):after {
    top: 142px;
    left: 96px;
}

.lds-roller div:nth-child(4) {
    animation-delay: -0.144s;
}

.lds-roller div:nth-child(4):after {
    top: 144px;
    left: 80px;
}

.lds-roller div:nth-child(5) {
    animation-delay: -0.18s;
}

.lds-roller div:nth-child(5):after {
    top: 142px;
    left: 64px;
}

.lds-roller div:nth-child(6) {
    animation-delay: -0.216s;
}

.lds-roller div:nth-child(6):after {
    top: 136px;
    left: 48px;
}

.lds-roller div:nth-child(7) {
    animation-delay: -0.252s;
}

.lds-roller div:nth-child(7):after {
    top: 126px;
    left: 34px;
}

.lds-roller div:nth-child(8) {
    animation-delay: -0.288s;
}

.lds-roller div:nth-child(8):after {
    top: 112px;
    left: 24px;
}

@keyframes lds-roller {
    0% {
        transform: rotate(0deg);
    }

    100% {
        transform: rotate(360deg);
    }
}

/* End animations */

.main-container.background {
    background-color: var(--main-logo-background);
}

.main-background {
    padding-left: 20px;
    padding-right: 20px;
    padding-top: 10px;
    padding-bottom: 70px;
    min-height: calc(100vh - 220px);
    overflow-y: auto;
    border-top-left-radius: 0px;
    border-top-right-radius: 0px;
    background-color: var(--white);
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
}

.main-content {
    max-width: 700px;
}

.header-container {
    display: grid;
    grid-template-columns: auto auto;
    place-items: center;
    align-items: center;
    justify-content: center;
    margin-left: 10px;
    margin-top: 52px;
    margin-bottom: 30px;
}


.logoTitle {
    color: var(--main-logo-background);
    font-size: var(--lg);
    font-weight: 700;
    padding: 30px 0px 30px 30px;
}

.signin {
    width: 100vw;
    height: 100%;
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
}

.ssid {
    margin-top: 104px;
    width: 100%;
    height: 100%;
    display: grid;
    place-items: center;
}

.device {
    margin-top: 104px;
}

/* The switch - the box around the slider */
.switch-container {
    display: flex;
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
}

.switch {
    position: relative;
    display: inline-block;
    width: 60px;
    height: 34px;
}

/* Hide default HTML checkbox */
.switch input {
    opacity: 0;
    width: 0;
    height: 0;
}

/* The slider */
.slider {
    position: absolute;
    cursor: pointer;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: #ccc;
    -webkit-transition: .4s;
    transition: .4s;
}

.slider:before {
    position: absolute;
    content: "";
    height: 26px;
    width: 26px;
    left: 4px;
    bottom: 4px;
    background-color: white;
    -webkit-transition: .4s;
    transition: .4s;
}

input:checked+.slider {
    background-color: var(--seconday-color);
}

input:focus+.slider {
    box-shadow: 0 0 1px var(--seconday-color);
}

input:checked+.slider:before {
    -webkit-transform: translateX(26px);
    -ms-transform: translateX(26px);
    transform: translateX(26px);
}

/* Rounded sliders */
.slider.round {
    border-radius: 34px;
}

.slider.round:before {
    border-radius: 50%;
}

.switch-text {
    font-size: var(--md);
}

/* End switch */

.ap {
    display: none;
}

.form-container {
    display: grid;
    grid-template-rows: auto 1fr auto;
}

.title {
    font-size: var(--lg);
    text-align: center;
    margin-bottom: 64px;
    font-weight: 700;
}

.subtitle {
    height: 48px;
    color: var(--text-field);
    font-size: var(--md);
    text-align: center;
    margin-top: 0;
    margin-bottom: 20px;
}

.i-title-container {
    width: 193px;
}

.i-title {
    color: var(--text-section);
}

.btn-back-container {
    display: flex;
    flex-direction: row;
    justify-content: space-around;
}

.empty-div {
    width: 300px;
}

.btn-back {
    margin: 30px 0px 0px 30px;
}

.form {
    display: flex;
    flex-direction: column;
    align-items: center;
}

input::-webkit-outer-spin-button,
input::-webkit-inner-spin-button {
    display: none;
}

.input {
    width: 365px;
    height: 26px;
    background-color: var(--fill-input-field);
    border: 1px solid var(--border);
    outline: none;
    border-radius: 27px;
    font-size: var(--sm);
    padding: 5px 5px 5px 20px;
    margin-bottom: 24px;
    color: var(--text);
}

.inputC {
    width: 380px;
    height: 26px;
    background-color: var(--fill-input-field);
    border: 1px solid var(--border);
    outline: none;
    border-radius: 5px;
    font-size: var(--sm);
    padding: 5px;
    margin-bottom: 24px;
    color: var(--text);
}

.input:hover {
    box-shadow: 0 1px 6px 0 #20212447;
    border-color: #dfe1e500;
}

.input-username::placeholder {
    /* background-image: url(./user.png); */
    background-repeat: no-repeat;
    background-position: left;
    background-size: contain;
}

.input-password::placeholder,
.input-pass::placeholder {
    /* background-image: url(./pass.png); */
    background-repeat: no-repeat;
    background-position: left;
    background-size: contain;
    margin-bottom: 30px;
}

.input-ap::placeholder {
    /* background-image: url(./ap.png); */
    background-repeat: no-repeat;
    background-position: left;
    background-size: contain;
    margin-bottom: 30px;
}

.header-home {
    height: 90px;
    display: flex;
    flex-direction: row;
    align-items: center;
    justify-content: space-around;
    background-color: var(--main-logo-background);
    color: var(--white);
}

.header-home-hide {
    display: none;
}

.left {
    display: flex;
    flex-direction: row;
    align-items: center;
    justify-content: center;
}

.right {
    display: flex;
    flex-direction: row;
}

/* Set 5px padding to the right elements */
.right>* {
    padding: 0 10px;
}

.logoP {
    margin: 20px;
}

.logoPTitle {
    vertical-align: middle;
    text-align: left;
    font-size: var(--md);
    font-weight: 700;
}

.logoPTitle>* {
    color: var(--white);
}

.menu-container {
    margin: 0 auto;
    display: grid;
    width: 500px;
    height: 340px;
    border-radius: 50px;
    grid-template-columns: 1fr 1fr;
    grid-template-rows: 1fr 1fr;
    place-items: center;
    align-items: center;
    text-align: center;
    align-content: center;
}

a {
    margin-top: 32px;
    text-decoration: none;
    color: var(--text);
    font-size: var(--md);
    font-weight: 700;
    place-content: center;
}

.config-header {
    margin: 54px auto;
    text-align: center;
    font-size: var(--lg);
    font-weight: normal;
    margin-bottom: 64px;
}

.card-container {
    margin: 0 auto;
    margin-top: 40px;
    display: grid;
    max-width: 380px;
    grid-auto-flow: column;
    grid-template-columns: 1fr 100px;
    justify-content: center;
    align-items: center;
}

.card-icon {
    text-align: right;
}

.mags-container,
.nfc-container {
    margin-top: 40px;
    display: flex;
    margin: 0 auto;
    flex-direction: column;
    align-items: center;
}

.card-form,
.read-form {
    margin-top: 50px;
    display: grid;
    grid-template-columns: 1fr 1fr;
    grid-template-rows: 1fr 1fr 1fr 1fr;
    grid-template-areas: "header header" "main main" "bottom1 bottom1" "bottom2 bottom2";
    align-items: center;
    justify-items: center;
    margin-bottom: 90px;
}

.read-form {
    grid-template-rows: 1fr 1fr 1fr;
    grid-template-areas: "header header" "main1 main2" "bottom1 bottom1" "bottom2 bottom2";
}

.input-card,
.text-card {
    grid-area: header;
}

.text-card {
    border: none;
}

.input-track1 {
    grid-area: header;
}

.input-track2 {
    grid-area: main;
}

.input-date,
.text-date {
    grid-area: main1;
    margin-right: 8px;
    border: none;
}

.input-code,
.text-code {
    grid-area: main2;
    margin-left: 8px;
    border: none;
}

.text-date,
.text-code {
    font-size: var(--xsm);
    width: 175px;
}

.primary-button {
    width: 392px;
    height: 40px;
    background-color: var(--main-logo-background);
    border: none;
    border-radius: 24px;
    font-size: var(--md);
    color: var(--white);
    cursor: pointer;
}

.clear-button,
.save-button {
    grid-area: bottom1;
    width: 392px;
    background-color: var(--white);
    border: 1px solid var(--border);
    border-radius: 24px;
    font-size: var(--md);
    color: var(--text);
}

.send-button,
.read-button {
    grid-area: bottom2;
    width: 392px;
}

/* Emulate button */

.emulate-nfc-container {
    width: 392px;
}

.nfc-animation-container {
    width: 160px;
    height: 160px;
}

.emulate-nfc-button-container {
    all: unset;
    width: 160px;
    height: 160px;
    position: absolute;
    z-index: 2;
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
}

.emulate-nfc-button {
    all: unset;
    height: 30px;
    font-size: var(--lg);
    background-color: #f0f0f04e;
    padding: 3px 10px 3px 10px;
    border: 1px solid #0000001a;
    border-radius: 15px;
    cursor: pointer;
}

.scroller {
    position: absolute;
    z-index: 1;
}

/* End Emulate button */

/* Footer */

.footer-home {
    position: fixed;
    width: 100%;
    height: 60px;
    bottom: 0;
    border-top: 1px solid var(--border);
    background-color: var(--white);
    place-items: center;
    align-content: center;
    display: none;
    z-index: 3;
}

.footer-icon-link {
    display: flex;
    flex-direction: column;
    align-items: center;
}

.footer-icon-text {
    margin-top: 0px;
    margin-bottom: 30px;
    color: var(--gray-text);
    font-size: var(--sm);
}

.store-text {
    width: 30px;
    margin: 0px 0px 30px 2px;
}

.selected {
    color: var(--main-logo-background);
}

@media (max-width: 640px) {
    .header-container {
        margin-left: 0px;
    }

    .header-home {
        height: 160px;
        justify-content: start;
    }

    .right {
        display: none;
    }

    .main-background {
        justify-content: start;
        border-top-left-radius: 30px;
        border-top-right-radius: 30px;
    }

    .emulate-nfc-container {
        width: 292px;
    }

    .logo {
        display: block;
    }

    .logoTitle {
        display: block;
    }

    .input {
        width: 265px;
    }

    .inputC {
        width: 280px;
    }

    .menu-container {
        width: 320px;
    }

    .card-container {
        min-width: 280px;
    }

    .text-card {
        border: none;
    }

    .text-date,
    .text-code {
        font-size: var(--xsm);
        width: 128px;
    }

    .primary-button,
    .clear-button,
    .save-button,
    .send-button,
    .read-button {
        width: 292px;
    }

    .footer-home {
        display: block;
        display: grid;
        grid-template-columns: 1fr 1fr 1fr;
    }
}

@media (min-width: 800px) {
    .header-home {
        /* padding-left: 100px; */
    }
})=====";