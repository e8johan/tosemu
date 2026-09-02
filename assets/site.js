/* TOSEMU project page - video embed and screenshot placeholders */

(function () {
  "use strict";

  /* ---------------------------------------------------------------
     The YouTube video. Set to "" to show a "video coming" note
     instead, or replace with another id to change the video.
     --------------------------------------------------------------- */
  var VIDEO_ID = "haVbsgpl1cM";

  /* The player is not loaded until it is clicked: until then this is a
     still and a button, so opening the page does not contact YouTube. */
  function mountVideo() {
    var slot = document.getElementById("video-slot");
    if (!slot) return;

    if (!VIDEO_ID) {
      slot.innerHTML =
        '<p class="empty">The video will be embedded here.<br>' +
        "Set VIDEO_ID in assets/site.js.</p>";
      return;
    }

    var facade = document.createElement("div");
    facade.className = "video-facade";
    facade.setAttribute("role", "button");
    facade.setAttribute("tabindex", "0");
    facade.setAttribute("aria-label", "Play the TOSEMU video on YouTube");

    var thumb = document.createElement("img");
    thumb.className = "thumb";
    thumb.alt = "";
    thumb.loading = "lazy";
    thumb.src = "https://i.ytimg.com/vi/" + VIDEO_ID + "/maxresdefault.jpg";
    /* maxresdefault does not exist for every video; hqdefault always does. */
    thumb.addEventListener("error", function onErr() {
      thumb.removeEventListener("error", onErr);
      thumb.src = "https://i.ytimg.com/vi/" + VIDEO_ID + "/hqdefault.jpg";
    });

    var play = document.createElement("button");
    play.className = "play";
    play.type = "button";
    play.setAttribute("aria-label", "Play video");

    facade.appendChild(thumb);
    facade.appendChild(play);
    slot.appendChild(facade);

    function load() {
      var frame = document.createElement("iframe");
      frame.src =
        "https://www.youtube-nocookie.com/embed/" + VIDEO_ID + "?autoplay=1&rel=0";
      frame.title = "TOSEMU in action";
      frame.allow =
        "accelerometer; autoplay; clipboard-write; encrypted-media; picture-in-picture";
      frame.allowFullscreen = true;
      slot.replaceChildren(frame);
    }

    facade.addEventListener("click", load);
    facade.addEventListener("keydown", function (e) {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        load();
      }
    });
  }

  /* A screenshot that is not in images/ yet leaves a labelled box rather
     than a broken image icon, so the page reads properly while the
     pictures are still being taken. */
  function markMissingShots() {
    var shots = document.querySelectorAll(".shot img");
    Array.prototype.forEach.call(shots, function (img) {
      function miss() {
        var fig = img.closest(".shot");
        if (!fig || fig.classList.contains("missing")) return;
        fig.classList.add("missing");
        var box = document.createElement("div");
        box.className = "placeholder";
        var label = document.createElement("span");
        label.textContent = "screenshot pending\n" + img.getAttribute("src");
        label.style.whiteSpace = "pre-line";
        box.appendChild(label);
        img.parentNode.insertBefore(box, img);
      }
      if (img.complete && img.naturalWidth === 0) miss();
      img.addEventListener("error", miss);
    });
  }

  mountVideo();
  markMissingShots();
})();
