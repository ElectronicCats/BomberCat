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

.primary-button {
    width: 250px;
    height: 40px;
    background-color: #1A5157;
    border: none;
    border-radius: 24px;
}
)=====";