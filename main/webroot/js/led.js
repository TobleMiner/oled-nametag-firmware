'use strict';

$(function() {
  $(".js-input-display-pattern").change(function() {
    syncSwitchState.bind(this)("/api/v1/set_enable_leds?enable=");
  });
  $(".js-input-disable-update").change(function() {
    syncSwitchState.bind(this)("/api/v1/set_led_update_disable?disable=");
  });
});

function syncSwitchState(uri) {
  var val = $(this).prop("checked") ? "1" : "0";

  $.get(uri + val);
}
