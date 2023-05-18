const char* main_js = R"=====(// Home
let btnConfig = document.querySelector("#btnConfig");
let btnNfc = document.querySelector("#btnNfc");

if (btnConfig != null) {
    btnConfig.addEventListener("click", () => { alert("Not available yet!") });
}

// if (btnNfc != null) {
//     btnNfc.addEventListener("click", () => { alert("Not available yet!") });
// }

// Magspoof
let btnSave = document.querySelector("#btnSave");
let btnEmulate = document.querySelector("#btnEmulate");
let btnField = document.querySelector("#btnField");

if (btnSave != null) {
    btnSave.addEventListener("click", () => {
        btnField.setAttribute("value", "Save");
    });
}

if (btnEmulate != null) {
    btnEmulate.addEventListener("click", () => {
        btnField.setAttribute("value", "Emulate");
    });
}

// Info
let btnSendMail = document.querySelector("#btnSendMail");

if (btnSendMail != null) {
    btnSendMail.addEventListener("click", () => { alert("Not available yet!") });
})=====";