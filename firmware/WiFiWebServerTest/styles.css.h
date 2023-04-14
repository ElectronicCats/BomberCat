const char* styles_css = R"=====(
:root {
    --main-logo-background: #1A5157;
    --text-section: #333333;
    --fill-input-field: #F8F9FC;
    --border: #aeaeae;
    --white: #FFFFFF;
    --text: #000000;
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

.header-container {
    display: grid;
    grid-template-columns: auto auto;
    place-items: center;
    align-items: center;
    justify-content: center;
    margin-top: 52px;
    margin-bottom: 30px;
}

.logoTitle {
    color: var(--main-logo-background);
    font-size: var(--lg);
    font-weight: 700;
}

.signin {
    width: 100%;
    height: 100%;
    display: grid;
    place-items: center;
}

.main-container {
    width: 300px;
    margin: 0 auto;
    display: flex;
    justify-content: center;
    place-items: center;
    align-items: center;
}

.ssid {
    margin-top: 104px;
    width: 100%;
    height: 100%;
    display: grid;
    place-items: center;
}

.ap {
    display: none;
}

.form-container {
    display: grid;
    grid-template-rows: auto 1fr auto;
    width: 300px;
}

.logo {
    width: 100px;
    height: 100px;
    margin-right: 30px; 
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

.i-title {
    color: var(--text-section);
}

.form {
    display: flex;
    flex-direction: column;
    align-items: center;
    align-self: center;
}

input::-webkit-outer-spin-button, input::-webkit-inner-spin-button {
    display: none;
}

.input {
    width: 250px;
    height: 26px;
    background-color: var(--fill-input-field);
    border: 1px solid var(--border);
    outline: none;
    border-radius: 27px;
    font-size: var(--sm);
    padding: 5px;
    margin-bottom: 24px;
    color: var(--text);
}

.inputC {
    width: 250px;
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

.input-ap, .input-pass {
    margin-left: 19px;
}

.input:hover {
    box-shadow: 0 1px 6px 0 #20212447;
    border-color: #dfe1e500;
}

.input-username::placeholder {
    background-image: url(./user.png);
    background-repeat: no-repeat;
    background-position: left;
    background-size: contain;
}

.input-password::placeholder, .input-pass::placeholder {
    background-image: url(./pass.png);
    background-repeat: no-repeat;
    background-position: left;
    background-size: contain;
    margin-bottom: 30px;
}

.input-ap::placeholder {
    background-image: url(./ap.png);
    background-repeat: no-repeat;
    background-position: left;
    background-size: contain;
    margin-bottom: 30px;
}

.primary-button {
    width: 250px;
    height: 40px;
    background-color: var(--main-logo-background);
    border: none;
    border-radius: 24px;
    font-size: var(--md);
    color: var(--white);
}

.ok-button {
    margin-left: 25px;
}

.header-home {
    margin: 0 auto;
    width: 100%;
    height: 180px;
    display: flex;
    flex-direction: row;
    place-items: center;
    background-color: var(--main-logo-background);
    color: var(--white);
    margin-bottom: 80px;
}

.logoP {
    margin: 20px;
}

.logoPTitle{
    vertical-align: middle;
    text-align: left;
    font-size: var(--md);
    font-weight: 700;
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

.footer-home {
    margin-top: 72px;
    position: fixed;
    width: 100%;
    height: 60px;
    bottom: 0;
    border-top: 1px solid var(--nav-bar);
    background-color: var(--white);
    place-items: center;
    align-content: center;
    display: none;
}

.config-header {
    margin: 54px auto;
    width: 500px;
    text-align: center;
    font-size: var(--lg);
    font-weight: normal;
    margin-bottom: 64px;
}

.card-container {
    margin: 0 auto;
    margin-top: 40px;
    display: grid;
    width: 500px;
    grid-auto-flow: column;
    grid-template-columns: 1fr 100px;
    justify-content: end;
    align-items: center;  
}

.input-card, .text-card {
   grid-area: header;
   width: 280px;
}

.input-date, .text-date {
    grid-area: main1;
    width: 125px;
    margin-right: 8px;
}

.text-date, .text-code {
    font-size: var(--xsm);
}

.input-code, .text-code {
    grid-area: main2;
    width: 125px;
    margin-left: 8px;
}

.send-button, .read-button {
    grid-area: footer;
    width: 200px;
}

.mags-container, .nfc-container {
    margin-top: 40px;
    display: flex;
    width: 500px;
    margin: 0 auto;
    flex-direction: column;
    align-items: center;
}

.card-form, .read-form {
    margin-top: 50px;
    display: grid;
    grid-template-columns: 1fr 1fr;
    grid-template-rows: 1fr 1fr 1fr;
    grid-template-areas: "header header" "main1 main2" "footer footer";
    align-items: center;
    justify-items: center;
    margin-bottom: 90px;
}

@media (max-width: 640px) {
    .logo{
        display: block;
    }
    .logoTitle{
        display: block;
    }  
    .menu-container {
        width: 320px;
    }
    .footer-home{
        display: block;
        display: grid;
        grid-template-columns: 1fr 1fr 1fr;
    }
    .card-container {
        margin: 0 auto;
        width: 400px;
        justify-items: center;
    }
}
)=====";