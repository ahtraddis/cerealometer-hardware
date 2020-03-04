#ifndef TEMPLATES_H
#define TEMPLATES_H

String styleHtmlTemplate = R"foo(
<style type="text/css">
  body, nav {background: #20162d;}
  .container {margin-left: 20px; margin-right: 20px; padding-bottom: 25px;}
  .center {text-align: center !important;}
  .right {text-align: right !important;}
  input[type=text], input[type=password] {width: 15em;}
  input.wide {width: 30em;}
  label {margin-bottom: 5px;}
  .toglab {font-size: 12px;}
  form small {color: #777}
  .wrapper {width: 800px; margin: 0 auto; background: #fff;}
  .hero {background: #eee; padding: 20px; border-radius: 10px; margin-top: 1em;}
  .vhcenter {display: -webkit-flexbox; display: -ms-flexbox; display: -webkit-flex;
    display: flex; -webkit-flex-align: center; -ms-flex-align: center;
    -webkit-align-items: center; align-items: center; justify-content: center;}
  .prompt {color: #fff; font-family: sans-serif;}
</style>)foo";

String scriptHtmlTemplate = R"foo(
<script type="text/javascript">
  function toggleShow(id) {
    var e = document.getElementById(id);
    e.type = (e.type === "password") ? "text" : "password";
  }
  function sendCal(id) {
    $.ajax({
      url: '/calib',
      type: 'POST',
      data: {
        id: id,
      },
      success: function(result) {
        console.log('cal result: ', result);
      }
    });
  }
</script>)foo";

String headerHtmlTemplate = R"foo(<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Cerealometer Admin</title>
    <link rel="stylesheet" type="text/css" href="//mincss.com/entireframework.min.css" />
    {STYLE}
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script>
    {SCRIPT}
  </head>
<body>
  <div class="wrapper">
    <nav class="nav" tabindex="-1" onclick="this.focus()">
      <div class="container">
        <a class="pagename current" href="/">Cerealometer</a>
        <a href="/settings">Settings</a>
        <a href="/calib">Calibration</a>
        <a href="/util">Utilities</a>
      </div>
    </nav>
    <button class="btn-close btn btn-sm">×</button>
    <div class="container">)foo";

String footerHtmlTemplate = R"foo(
      </div>
    </div>
  </body>
</html>)foo";

String settingsHtmlTemplate = R"foo(<h2>Settings</h2>
<form method="POST" action="/settings">
  <label>Wi-Fi SSID</label>
  <input type="text" class="smooth" name="wifi_ssid" length="31"
    value="{WIFI_SSID}" />
  <label for="wifi_password">Wi-Fi Password</label>
  <input id="wifi_password" type="password" class="smooth"
    name="wifi_password" length="63" value="{WIFI_PASSWORD}" />
  <span><input id="togglepass" type="checkbox" onclick="toggleShow('wifi_password')" />
  <label for="togglepass">Show Password</small></label>
  <label>Firebase Project ID</label>
  <input type="text" class="smooth wide" name="firebase_project_id"
    length="63" value="{FIREBASE_PROJECT_ID}" />
  <small>Example: my-project-0123456789</small>
  <label for="firebase_db_secret" class="toglab">Firebase Database Secret</label>
  <input id="firebase_db_secret" type="password" class="smooth wide"
    name="firebase_db_secret" length="63" value="{FIREBASE_DB_SECRET}" />
  <span><input id="togglesecret" type="checkbox" onclick="toggleShow('firebase_db_secret')" />
  <label for="togglesecret" class="toglab">Show Secret</label></span>
  <label>Device ID</label>
  <input type="text" class="smooth" name="device_id" length="31"
    value="{DEVICE_ID}" />
  <small>Unique key provided by the Cerealometer app</small>
  <input class="btn smooth btn-sm" value="Update" type="submit">
</form>)foo";

String calibrateHtmlTemplate = R"foo(
<h2>Calibration</h2>
<div class="hero">
  <p>To calibrate, first remove all items from the Shelf and click below to tare.
  Then set the 0.1 kg calibration weight on each scale in turn and click Calibrate.</p>
  <form method="POST" action="/calib">
  <input type="hidden" name="offsets" />
  <input type="submit" value="Tare scales" class="btn btn-sm smooth" />
  </form>
</div>
<table class="table">
  <thead>
    <tr>
      <th class="center">Slot</th>
      <th class="center">Weight (kg)</th>
      <th class="center">Tare Offset</th>
      <th class="center">Calibration Factor</th>
      <th class="center">Status</th>
      <th class="center">Calibrate</th>
    </tr>
  </thead>
<tbody>
  {TABLE_ROWS}
</tbody>
</table>
)foo";

String calibrateRowHtmlTemplate = R"foo(
<tr>
  <td class="right">{PORT}</td>
  <td class="right">{LAST_WEIGHT_KILOGRAMS}</td>
  <td class="right">{TARE_OFFSET}</td>
  <td class="right">{CALIBRATION_FACTOR}</td>
  <td class="center">{STATUS}</td>
  <td class="center">
    <button id="{PORT}" class="btn btn-sm smooth" type="button" \
      onClick="sendCal(this.id)">Calibrate</button>
  </td>
</tr>)foo";

String utilHtmlTemplate = R"foo(
<h2>Utilities</h2>
<form method="POST" action="/util">
  <input name="reboot" type="hidden" />
  <input class="btn smooth btn-sm" value="Reboot" type="submit" />
</form>
<form method="POST" action="/util">
  <input name="displaytest" type="hidden" />
  <input class="btn smooth btn-sm" value="Run display tests" type="submit" />
</form>)foo";

String loadingHtmlTemplate = R"foo(<!DOCTYPE html>
<html>
  <head>
    <title>Cerealometer Admin</title>
    <script src="{URL1}"></script>
    {STYLE}
    <style type="text/css">html, body, .spinner {height: 100%;}</style>
    <script type="text/javascript">
      setInterval(ping, 3000);
      function ping() {
        $.ajax({
          url: '/',
          success: function(result) {
            window.location.href = '/';
          }
        });
      }
    </script>
  </head>
  <body>
    <div class="spinner vhcenter">
      <div class="center">
        <h2 class="prompt">Rebooting... brb.</h2>
        <img src="{URL2}" />
      </div>
    </div>
  </body>
</html>)foo";

#endif