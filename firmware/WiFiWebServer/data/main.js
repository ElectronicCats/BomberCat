// Home
let btnConfig = document.querySelector("#btnConfig");
let btnNfc = document.querySelector("#btnNfc");

if (btnConfig != null) {
    btnConfig.addEventListener("click", () => { alert("Not available yet!") });
}

// if (btnNfc != null) {
//     btnNfc.addEventListener("click", () => { alert("Not available yet!") });
// }

// Magspoof
let magspoof = document.querySelector("#magspoof");
let magspoofForm = document.querySelector("#magspoofForm");
let btnSave = document.querySelector("#btnSave");
let btnEmulate = document.querySelector("#btnEmulate");
let btnField = document.querySelector("#btnField");
let track1 = document.querySelector("#track1");
let track2 = document.querySelector("#track2");
let currentLocation = localStorage.getItem("location");
let reload = localStorage.getItem("reload");
console.log("Reload: " + reload);
console.log("Location: " + currentLocation);

if (magspoof != null) {
    magspoofForm.addEventListener("submit", (event) => {
        event.preventDefault();
        // Save tracks in local storage
        localStorage.setItem("track1", track1.value);
        localStorage.setItem("track2", track2.value);

        if (track1.value == "") {
            alert("Track 1 is empty!");
        } else {
            currentLocation = `magspoof.html?track1=${track1.value}&track2=${track2.value}&button=${btnField.value}#`;
            localStorage.setItem("location", currentLocation);
            localStorage.setItem("counter", 0);
            localStorage.setItem("reload", true);
            window.location.reload();
        }
    });

    btnSave.addEventListener("click", (event) => {
        // btnField.setAttribute("value", "Save");
        event.preventDefault();
        alert("Not available yet!");
    });

    btnEmulate.addEventListener("click", () => {
        btnField.setAttribute("value", "Emulate");
    });

    // Load tracks from local storage
    track1.value = localStorage.getItem("track1");
    track2.value = localStorage.getItem("track2");

    if (reload == "true") {
        console.log("Here");
        if (localStorage.getItem("counter") == 0) {
            localStorage.setItem("counter", 1);
            window.location.href = currentLocation;
        }
        // Reload page to "magspoof.html" after 500 ms
        if (localStorage.getItem("counter") == 1) {
            setTimeout(() => {
                localStorage.setItem("reload", false);
                console.log("Reloaded!");
                window.location.replace("magspoof.html");
            }, 500);
        }
    }
}

// Info
let btnSendMail = document.querySelector("#btnSendMail");

if (btnSendMail != null) {
    btnSendMail.addEventListener("click", () => { alert("Not available yet!") });
}