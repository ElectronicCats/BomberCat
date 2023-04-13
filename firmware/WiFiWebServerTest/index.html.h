#include "styles.css.h"
#include "app.js.h"

String index_html = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <title>Login</title>
    <meta charset="utf-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1">

</head>
<body>
    <header>
        <div class="header-container">
            <img src="./Logo-100_100.png" alt="logo" class="logo">
            <h1 class="logoTitle">NFC Copy Cat</h1>
        </div>
    </header>
    <section class="signin">
        <div class="form-container">
            <h2 class="title">Sign in</h2>
            <p class="subtitle">Enter your username and password.</p>
            <form action="/" class="form">
                <p>
                    <input type="username" required id="username" placeholder="       Username" class="input input-username">
                    <input type="password" required id="password" placeholder="       Password" class="input input-password">
                    <input type="submit" value="Sign in" class="primary-button signin-button">
                </p>
            </form>
        </div>
    </section>
    <script>
        )=====" + app_js + R"=====(
    </script>
</body>
</html>
)=====";
