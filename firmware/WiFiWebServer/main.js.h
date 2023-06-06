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
let track1 = document.querySelector("#track1");
let track2 = document.querySelector("#track2");

if (btnSave != null) {
    btnSave.addEventListener("click", (event) => {
        // btnField.setAttribute("value", "Save");
        event.preventDefault();
        alert("Not available yet!");
    });

    btnEmulate.addEventListener("click", () => {
        btnField.setAttribute("value", "Emulate");
        
        // Save tracks in local storage
        localStorage.setItem("track1", track1.value);
        localStorage.setItem("track2", track2.value);
    });

    // Load tracks from local storage
    track1.value = localStorage.getItem("track1");
    track2.value = localStorage.getItem("track2");
}

// Info
let btnSendMail = document.querySelector("#btnSendMail");

if (btnSendMail != null) {
    btnSendMail.addEventListener("click", () => { alert("Not available yet!") });
})=====";