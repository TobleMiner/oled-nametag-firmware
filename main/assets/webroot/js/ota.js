'use strict';

$(function() {
  $(".js-upload-ota-form").submit(function(event) {
    var input = $(".js-upload-ota-file")[0];
    var file = input.files[0];

    if (!file) {
      event.preventDefault();
      event.stopPropagation();
      return false;
    }

    var reader = new FileReader();
    reader.onload = function(event) {
      var data = event.target.result;
      var view = new Uint8Array(data);
      var hex = encodeHex(view);

      $.ajax({
        url: "/api/v1/upload_ota",
        method: "POST",
        contentType: "application/x-www-form-urlencoded",
        data: hex,
        dataType: "json",
        xhr: function() {
          var xhr = $.ajaxSettings.xhr();

          xhr.upload.onprogress = function(event) {
            if (event.lengthComputable) {
              var progress = Math.ceil(event.loaded / event.total * 100)

              $(".js-upload-progress").css("width", progress + "%").attr("aria-valuenow", progress);
              console.log("Progress: " + event.loaded / event.total);
            }
          };

          return xhr;
        }
      })
      .done(function() {
        location.reload(true);
      })
      .fail(function(xhr) {
        $(".js-upload-progress").addClass("bg-danger");
        if (xhr.responseJSON && xhr.responseJSON["error"]) { 
          addToast("Upload failed", xhr.responseJSON["error"]);
        }
      })
      .always(function() {
        $(".js-upload-progress").css("width", "100%").attr("aria-valuenow", "100");
        $(".js-modal-upload").modal('hide');
      });
    };
    $(".js-upload-progress").css("width", "0%").attr("aria-valuenow", "0").removeClass("bg-danger");
    $(".js-modal-upload").modal('show');
    reader.readAsArrayBuffer(file);

    event.preventDefault();
    return false;
  });
  $(".js-reboot-form").submit(function(event) {
    var progress = 0;

    $(".js-reboot-progress").css("width", "0%").attr("aria-valuenow", "0").removeClass("bg-danger");
    $(".js-modal-reboot").modal('show');

    var progress_cb = function() {
      $(".js-reboot-progress").css("width", progress + "%").attr("aria-valuenow", progress);
      if (progress >= 100) {
        $(".js-modal-reboot").modal('hide');
        location.reload(true);
      } else {
        progress++;
        setTimeout(progress_cb, 50);
      }
    };

    $.get("/api/v1/reboot");
    progress_cb();

    event.preventDefault();
    return false;
  });

  $(".js-activate-booted-form").submit(function(event) {
    $.get("/api/v1/activate_booted_partition");
  });

  $(".js-activate-standby-form").submit(function(event) {
    $.get("/api/v1/activate_standby_partition");
  });
});

function addToast(title, body) {
  var container = $(".js-toasts");
  var toastHtml = $('\
<div class="toast" role="alert" aria-live="assertive" aria-atomic="true"> \
  <div class="toast-header bg-danger text-white"> \
    <strong class="me-auto">' + title +'</strong> \
    <button type="button" class="btn-close" data-bs-dismiss="toast" aria-label="Close"></button> \
  </div> \
  <div class="toast-body">' + body + '</div> \
</div>');

  toastHtml.appendTo(container);
  toastHtml.toast('show');
  setTimeout(function() {
    toastHtml.remove();
  }, 30000);
}

function intToHexNibble(i) {
  if (i < 10) {
    return String.fromCharCode("0".charCodeAt(0) + i);
  } else {
    return String.fromCharCode("A".charCodeAt(0) + (i - 10));
  }
}

function encodeHex(data) {
  var hexstring = "";

  for (var i = 0; i < data.length; i++) {
    hexstring += intToHexNibble(data[i] >> 4);
    hexstring += intToHexNibble(data[i] & 0x0f);
  }

  return hexstring;
}
