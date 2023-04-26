'use strict';

$(function() {
  $(".js-upload-animation-form").submit(function(event) {
    var input = $(".js-upload-animation-file")[0];
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
      var encodedFilename = encodeURIComponent(file.name);

      $.ajax({
        url: "/api/v1/upload_animation?filename=" + encodedFilename,
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

  ajaxLoadAnimations();
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

function selectActiveAnimation(filename) {
  $(".js-animations").children().each(function() {
    var elem = $(this);
    if (elem.hasClass("js-animation")) {
      if (elem.attr("data-animation") == filename) {
        elem.attr("aria-current", "true");
        elem.addClass("bg-primary");
        elem.addClass("text-white");
      } else {
        elem.removeAttr("aria-current");
        elem.removeClass("bg-primary");
        elem.removeClass("text-white");
      }
    }
  });
}

function addAnimation(filename, active) {
  var elem = $("<a href=\"#\" class=\"d-flex align-items-center list-group-item js-animation\"><span class=\"me-3 text-break\">" + filename + "</span></a>");
  elem.attr("data-animation", filename);
  if (active) {
    elem.attr("aria-current", "true");
    elem.addClass("bg-primary");
    elem.addClass("text-white");
  }
  elem.click(function(fname, event) {
    var encodedFilename = encodeURIComponent(fname);

    $.get("/api/v1/set_animation?filename=" + encodedFilename).done(function() {
      selectActiveAnimation(fname);
    });
    event.preventDefault();
    return false;
  }.bind(elem, filename));

  var button = $("<input class=\"btn btn-danger ms-auto\" type=\"button\" value=\"Delete\">");
  button.click(function(fname, entry, event) {
    var encodedFilename = encodeURIComponent(fname);

    $.get("/api/v1/delete_animation?filename=" + encodedFilename).done(function() {
	    entry.remove();
    });
    event.preventDefault();
    return false;
  }.bind(button, filename, elem));
  elem.append(button);
  $(".js-animations").append(elem);
}

function ajaxLoadAnimations() {
  $.get({ url: "/api/v1/animations", dataType: "json"}).done(function(data) {
    var animations = data["animations"];

    $(".js-animations-loading").remove();
    for (var idx in animations) {
      var animation = animations[idx];

      addAnimation(animation["name"], "active" in animation && animation["active"]);
    }
  });
}
