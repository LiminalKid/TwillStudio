// Runs before the app starts (linked with --pre-js). The app's files (autosave, settings, saved
// projects, renders) live in /twill, which is kept in the browser's IndexedDB so they survive a
// reload. They are read in before the app starts; the app calls Module.twillPersist() after each
// write to copy them back.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  FS.mkdir('/twill');
  FS.mount(IDBFS, {}, '/twill');
  addRunDependency('twill-files');
  FS.syncfs(true, function (err) {
    if (err) console.warn('Twill: could not read saved files', err);
    removeRunDependency('twill-files');
  });
});

// One copy to IndexedDB at a time; writes made during a copy are picked up by one more.
Module.twillPersist = (function () {
  var busy = false, again = false;
  function run() {
    busy = true;
    FS.syncfs(false, function (err) {
      if (err) console.warn('Twill: could not save files', err);
      busy = false;
      if (again) { again = false; run(); }
    });
  }
  return function () { if (busy) again = true; else run(); };
})();
