#ifndef TEMPLATES_H
#define TEMPLATES_H

// Minified HTML template strings to assemble web pages
// Copy pretty markup and minify via http://minifycode.com/html-minifier/

/*
<style type="text/css">
  body, nav {background: #20162d;}
  .cntr {margin: 0 20px 0 20px; padding-bottom: 25px;}
  .c, tr.c th, td.c {text-align: center;}
  .r, td.r {text-align: right;}
  .d {opacity: .4}
  input[type=text], input[type=password] {width: 15em;}
  input.wide {width: 30em;}
  label {font-size: 14px; margin-bottom: 5px; font-weight: bold;}
  .toglab {font-size: smaller; font-weight: normal; color: #777; margin-left: .3em;}
  small {color: #777}
  .w {width: 800px; margin: 0 auto; background: #fff;}
  .h {background: #eee; padding: 20px; border-radius: 10px; margin-top: 1em;}
  .vhc {display: flex; align-items: center; justify-content: center;}
  .pr {color: #fff; font-family: sans-serif;}
</style>
*/
String styleHtmlTemplate = R"foo(<style type="text/css">body,nav{background:#20162d}.cntr{margin:0 20px 0 20px;padding-bottom:25px}.c, tr.c th,td.c{text-align:center}.r,td.r{text-align:right}.d{opacity: .4}input[type=text],input[type=password]{width:15em}input.wide{width:30em}label{font-size:14px;margin-bottom:5px;font-weight:bold}.toglab{font-size:smaller;font-weight:normal;color:#777;margin-left: .3em}small{color:#777}.w{width:800px;margin:0 auto;background:#fff}.h{background:#eee;padding:20px;border-radius:10px;margin-top:1em}.vhc{display:flex;align-items:center;justify-content:center}.pr{color:#fff;font-family:sans-serif}</style>)foo";

/*
<script type="text/javascript">
  function tog(id) {
    var e = document.getElementById(id);
    e.type = (e.type === "password") ? "text" : "password";
  }
  function sendCal(id) {
    $('td button:eq(' + id + ')').addClass('d').attr('disabled', true);
    $.ajax({
      url: '/calib',
      type: 'POST',
      data: {
        id: id
      },
      success: function(r) {
        $.find("td.f")[id].innerHTML = r;
        $('td button:eq(' + id + ')').removeClass('d').removeAttr('disabled');
      }
    });
  }
</script>
*/
String scriptHtmlTemplate = R"foo(
<script type="text/javascript">function tog(id){var e=document.getElementById(id);e.type=(e.type==="password")?"text":"password";} function sendCal(id){$('td button:eq('+id+')').addClass('d').attr('disabled',true);$.ajax({url:'/calib',type:'POST',data:{id:id},success:function(r){$.find("td.f")[id].innerHTML=r;$('td button:eq('+id+')').removeClass('d').removeAttr('disabled');}});}</script>)foo";

/*
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Cerealometer Admin</title>
    <link rel="stylesheet" type="text/css" href="//mincss.com/entireframework.min.css" />
    {STYLE}
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script>
    <script src="https://use.fontawesome.com/2b93418c06.js"></script>
    {SCRIPT}
  </head>
<body>
  <div class="w">
    <nav class="nav" tabindex="-1" onclick="this.focus()">
      <div class="cntr">
        <a class="pagename current" href="/">Cerealometer</a>
        <a href="/settings">Settings</a>
        <a href="/calib">Calibration</a>
        <a href="/util">Utilities</a>
      </div>
    </nav>
    <button class="btn-close btn btn-sm">×</button>
    <div class="cntr">
*/
String headerHtmlTemplate = R"foo(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Cerealometer Admin</title><link rel="stylesheet" type="text/css" href="//mincss.com/entireframework.min.css" /> {STYLE} <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script><script src="https://use.fontawesome.com/2b93418c06.js"></script> {SCRIPT}</head><body><div class="w"><nav class="nav" tabindex="-1" onclick="this.focus()"><div class="cntr"><a class="pagename current" href="/">Cerealometer</a><a href="/settings">Settings</a><a href="/calib">Calibration</a><a href="/util">Utilities</a></div></nav><button class="btn-close btn btn-sm">×</button><div class="cntr">)foo";

/*
      </div>
    </div>
  </body>
</html>
*/
String footerHtmlTemplate = R"foo(</div></div></body></html>)foo";

/*
<h2>Settings</h2>
<form method="POST" action="/settings">
  <label>Wi-Fi SSID</label>
  <input type="text" class="smooth" name="wifi_ssid" length="31"
    value="{WIFI_SSID}" />
  <label for="wpass">Wi-Fi Password</label>
  <input id="wpass" type="password" class="smooth"
    name="wifi_password" length="63" value="{WIFI_PASSWORD}" />
  <span>
    <input id="togpass" type="checkbox" onclick="tog('wpass')" />
    <label for="togpass" class="toglab">Show Password</label>
  </span>
  <label>Firebase Project ID</label>
  <input type="text" class="smooth wide" name="firebase_project_id"
    length="63" value="{FIREBASE_PROJECT_ID}" />
  <small>Example: my-project-0123456789</small>
  <label for="secret">Firebase Database Secret</label>
  <input id="secret" type="password" class="smooth wide"
    name="firebase_db_secret" length="63" value="{FIREBASE_DB_SECRET}" />
  <span>
    <input id="togsec" type="checkbox" onclick="tog('secret')" />
    <label for="togsec" class="toglab">Show Secret</label>
  </span>
  <label>Device ID</label>
  <input type="text" class="smooth" name="device_id" length="31"
    value="{DEVICE_ID}" />
  <small>Unique key provided by the Cerealometer app</small>
  <label for="ledon">Display Intensity</label>
  <input id="ledon" class="smooth"
    name="led_on_intensity" length="3" value="{LED_ON_INTENSITY}" />
  <small>0-255</small>
  <input class="btn smooth btn-sm" value="Update" type="submit">
</form>
*/
String settingsHtmlTemplate = R"foo(<h2>Settings</h2><form method="POST" action="/settings"><label>Wi-Fi SSID</label><input type="text" class="smooth" name="wifi_ssid" length="31" value="{WIFI_SSID}" /><label for="wpass">Wi-Fi Password</label><input id="wpass" type="password" class="smooth" name="wifi_password" length="63" value="{WIFI_PASSWORD}" /><span><input id="togpass" type="checkbox" onclick="tog('wpass')" /><label for="togpass" class="toglab">Show Password</label></span><label>Firebase Project ID</label><input type="text" class="smooth wide" name="firebase_project_id" length="63" value="{FIREBASE_PROJECT_ID}" /><small>Example: my-project-0123456789</small><label for="secret">Firebase Database Secret</label><input id="secret" type="password" class="smooth wide" name="firebase_db_secret" length="63" value="{FIREBASE_DB_SECRET}" /><span><input id="togsec" type="checkbox" onclick="tog('secret')" /><label for="togsec" class="toglab">Show Secret</label></span><label>Device ID</label><input type="text" class="smooth" name="device_id" length="31" value="{DEVICE_ID}" /><small>Unique key provided by the Cerealometer app</small><label for="ledon">Display Intensity</label><input id="ledon" class="smooth" name="led_on_intensity" length="3" value="{LED_ON_INTENSITY}" /><small>0-255</small><input class="btn smooth btn-sm" value="Update" type="submit"></form>)foo";

/*
<h2>Calibration</h2>
<div class="h">
  <p>To calibrate, first remove all items from the shelf and click Tare Scales.
  Then, set the 0.1 kg calibration weight on each scale in turn and click Calibrate.
  When finished, click Save Changes.</p>
  <form method="POST" action="/calib">
  <input type="submit" name="offsets" value="Tare Scales" class="btn btn-sm smooth" />
  <input type="submit" name="savecal" value="Save Changes" class="btn btn-sm smooth" />
  </form>
</div>
<table class="table">
  <thead>
    <tr class="c">
      <th>Slot</th>
      <th>Weight (kg)</th>
      <th>Tare Offset</th>
      <th>Calibration Factor</th>
      <th>Status</th>
      <th>Calibrate</th>
    </tr>
  </thead>
<tbody>
  {TABLE_ROWS}
</tbody>
</table>
*/

/*
<tr>
  <td class="r">{PORT}</td>
  <td class="r">{LAST_WEIGHT_KILOGRAMS}</td>
  <td class="r">{TARE_OFFSET}</td>
  <td class="f r">{CALIBRATION_FACTOR}</td>
  <td class="c">{STATUS}</td>
  <td class="c">
    <button id="{SLOT}" class="btn btn-sm smooth" type="button" \
      onClick="sendCal(this.id)">Calibrate</button>
  </td>
</tr>
*/
#ifdef ENABLE_CALIBRATE
String calibrateHtmlTemplate = R"foo(<h2>Calibration</h2><div class="h"><p>To calibrate, first remove all items from the shelf and click Tare Scales. Then, set the 0.1 kg calibration weight on each scale in turn and click Calibrate. When finished, click Save Changes.</p><form method="POST" action="/calib"> <input type="submit" name="offsets" value="Tare Scales" class="btn btn-sm smooth" /> <input type="submit" name="savecal" value="Save Changes" class="btn btn-sm smooth" /></form></div><table class="table"><thead><tr class="c"><th>Slot</th><th>Weight (kg)</th><th>Tare Offset</th><th>Calibration Factor</th><th>Status</th><th>Calibrate</th></tr></thead><tbody> {TABLE_ROWS}</tbody></table>)foo";
String calibrateRowHtmlTemplate = R"foo(<tr><td class="r">{PORT}</td><td class="r">{LAST_WEIGHT_KILOGRAMS}</td><td class="r">{TARE_OFFSET}</td><td class="f r">{CALIBRATION_FACTOR}</td><td class="c">{STATUS}</td><td class="c"><button id="{SLOT}" class="btn btn-sm smooth" type="button" onClick="sendCal(this.id)">Calibrate</button></td></tr>)foo";
#endif

/*
<h2>Utilities</h2>
<form method="POST" action="/util">
  <input class="btn smooth btn-sm" value="Reboot" name="reboot" type="submit" />
  <input class="btn smooth btn-sm" value="Run display tests" name="displaytest" type="submit" />
</form>
*/
String utilHtmlTemplate = R"foo(<h2>Utilities</h2><form method="POST" action="/util"> <input class="btn smooth btn-sm" value="Reboot" name="reboot" type="submit" /> <input class="btn smooth btn-sm" value="Run display tests" name="displaytest" type="submit" /></form>)foo";

/*
<!DOCTYPE html>
<html>
  <head>
    <title>Cerealometer Admin</title>
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script>
    <script src="https://use.fontawesome.com/2b93418c06.js"></script>
    {STYLE}
    <style type="text/css">html, body, .spin {height: 100%; color: #e69900}</style>
    <script type="text/javascript">
      setInterval(ping, 2000);
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
    <div class="spin vhc">
      <div class="c">
        <h2 class="pr">Rebooting... brb.</h2>
        <i class="fa fa-spinner fa-spin fa-5x" />
      </div>
    </div>
  </body>
</html>
*/
String loadingHtmlTemplate = R"foo(<!DOCTYPE html><html><head><title>Cerealometer Admin</title> <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script> <script src="https://use.fontawesome.com/2b93418c06.js"></script> {STYLE}<style type="text/css">html,body,.spin{height:100%;color:#e69900}</style> <script type="text/javascript">setInterval(ping,2000);function ping(){$.ajax({url:'/',success:function(result){window.location.href='/';}});}</script> </head><body><div class="spin vhc"><div class="c"><h2 class="pr">Rebooting... brb.</h2> <i class="fa fa-spinner fa-spin fa-5x" /></div></div></body></html>)foo";

#endif