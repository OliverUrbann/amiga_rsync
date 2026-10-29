/* rbackup.rexx - back up Amiga drives to an rsync server, with retries
   $VER: rbackup.rexx 1.0 (29.10.2026)

   Start it from a Shell with   rx rbackup.rexx
   (RexxMast must be running, the standard Workbench starts it).
   Edit the settings below first. */

/* ======================= Settings ======================= */

server   = 'myserver.example.org'  /* name or IP, may end in :port */
module   = 'amiga'                 /* module name in the server's rsyncd.conf */
user     = ''                      /* leave empty if the module has no "auth users" */
password = ''

/* What to back up, one line each, written the Amiga way.
   Each one lands in a folder of the same name in the module:
   'Work:' goes to amiga/Work/, 'Data:Pictures' to amiga/Data/Pictures/.
   Number the lines 1, 2, 3, ... without gaps. */
backup.1 = 'Work:'
backup.2 = 'DH0:'
/* backup.3 = 'Data:Pictures' */

/* Time zone of the Amiga clock, so the dates on the server are right. */
utcoffset  = 1       /* hours ahead of UTC in winter: Germany 1, UK 0,
                        Finland 2, New York -5; '' = use ENV:TZ as it is */
summertime = 'EU'    /* 'EU' (all of Europe), 'US' or 'none' */

/* Set to 1 for ONE run after you first set the time zone, if the server
   already holds a backup made without it: that run only corrects the
   dates (rsync --size-only) instead of reading every file again.
   Then set it back to 0. */
fixdates = 0

options   = '-rtXlH --partial --timeout=120'
tries     = 5                 /* attempts per drive on network errors */
pause     = 30                /* seconds before the 1st retry, then 2x, 3x, ... */
logfile   = 'RAM:rbackup.log' /* rsync's own log and the results of this script */
requester = 1                 /* 1 = show a summary window at the end */
rsync     = 'rsync'           /* the rsync program, e.g. 'C:rsync' */
timezone  = ''                /* a full TZ value instead of utcoffset and
                                 summertime, e.g. 'CET-1CEST,M3.5.0,M10.5.0/3' */

/* ==================== End of settings ==================== */

SIGNAL ON BREAK_C

IF timezone = '' & utcoffset ~= '' THEN
   timezone = make_tz(utcoffset, summertime)
IF timezone ~= '' THEN
   ADDRESS COMMAND 'SetEnv TZ "'timezone'"'
ELSE IF ~EXISTS('ENV:TZ') THEN
   CALL note 'Warning: no time zone set, the dates on the server will be off.'

IF fixdates = 1 THEN DO
   options = options '--size-only'
   CALL note 'fixdates = 1: only correcting dates. Set it back to 0 afterwards.'
END

IF password ~= '' THEN
   ADDRESS COMMAND 'SetEnv RSYNC_PASSWORD "'password'"'

url = 'rsync://'
IF user ~= '' THEN url = url || user'@'
url = url || server'/'module'/'

worst = 0
summary = ''
IF SYMBOL('backup.1') ~= 'VAR' THEN DO
   CALL note 'Nothing to back up: no backup.1 in the settings.'
   worst = 20
END
DO i = 1 WHILE SYMBOL('backup.'i) = 'VAR'
   r = backup_one(backup.i)
   IF r = 'NF' THEN DO
      text = 'not found on this Amiga'
      r = 23
   END
   ELSE text = meaning(r)
   IF r > worst THEN worst = r
   summary = summary || backup.i '-' text'*N'
   IF r = 5 THEN DO   /* a wrong login fails for every drive */
      summary = summary || 'Stopped after the failed login.*N'
      LEAVE
   END
END

CALL finish
EXIT worst

/* Back up one drive or drawer, retrying on network errors. */
backup_one: PROCEDURE EXPOSE rsync options url tries pause logfile
   PARSE ARG what
   /* Missing drives would bring up an "insert volume" requester:
      check the name quietly first. */
   PARSE VAR what vol ':' .
   ADDRESS COMMAND 'Assign >NIL: "'vol':" EXISTS'
   IF RC ~= 0 THEN DO
      CALL note DATE() TIME() what '-> not found on this Amiga'
      RETURN 'NF'
   END
   IF ~EXISTS(what) THEN DO
      CALL note DATE() TIME() what '-> not found on this Amiga'
      RETURN 'NF'
   END
   path = unix_path(what)
   opts = options
   /* A drawer (Data:Pictures) needs its parent folders on the server;
      --mkpath creates them (server: rsync 3.2.3 or newer). */
   IF LENGTH(path) - LENGTH(COMPRESS(path, '/')) > 1 THEN
      opts = opts '--mkpath'
   DO try = 1 TO tries
      CALL note DATE() TIME() what '-> try' try 'of' tries
      ADDRESS COMMAND rsync opts '"--log-file='logfile'"',
                      '"/'path'"' '"'url || path'"'
      r = RC
      CALL note DATE() TIME() what '->' meaning(r) '(code' r')'
      IF r = 0 | ~retry_on(r) THEN RETURN r
      IF try < tries THEN DO
         CALL note 'Waiting' pause * try 'seconds before the next try'
         ADDRESS COMMAND 'Wait' pause * try 'SECS'
      END
   END
   RETURN r

/* 'Work:' -> 'Work/', 'Data:Pictures' -> 'Data/Pictures/' */
unix_path: PROCEDURE
   PARSE ARG vol ':' rest
   rest = STRIP(STRIP(rest, 'B', '/'))
   IF rest = '' THEN RETURN vol'/'
   RETURN vol'/'rest'/'

/* utcoffset and summertime as a POSIX TZ value, e.g. 1 and 'EU' give
   'STD-1DST,M3.5.0/2,M10.5.0/3'.  EU: last Sunday of March and of
   October at 01:00 UTC; US: 2nd Sunday of March, 1st Sunday of November
   at 02:00 local time. */
make_tz: PROCEDURE
   PARSE ARG off, st
   tz = 'STD' || hours(-off)
   SELECT
      WHEN UPPER(st) = 'EU' THEN
         RETURN tz'DST,M3.5.0/'hours(1 + off)',M10.5.0/'hours(2 + off)
      WHEN UPPER(st) = 'US' THEN
         RETURN tz'DST,M3.2.0,M11.1.0'
      OTHERWISE
         RETURN tz
   END

/* 1 -> '1', -5 -> '-5', 5.5 -> '5:30' */
hours: PROCEDURE
   PARSE ARG h
   sign = ''
   IF h < 0 THEN DO
      sign = '-'
      h = -h
   END
   m = (h - TRUNC(h)) * 60
   IF m = 0 THEN RETURN sign || TRUNC(h)
   RETURN sign || TRUNC(h)':'RIGHT(TRUNC(m), 2, '0')

/* Exit codes worth another try: network, broken connection, timeouts. */
retry_on: PROCEDURE
   RETURN FIND('10 12 30 35', ARG(1)) > 0

meaning: PROCEDURE
   SELECT
      WHEN ARG(1) = 0  THEN RETURN 'OK'
      WHEN ARG(1) = 5  THEN RETURN 'login failed (user, password, module?)'
      WHEN ARG(1) = 10 THEN RETURN 'network error'
      WHEN ARG(1) = 12 THEN RETURN 'connection broken off'
      WHEN ARG(1) = 23 THEN RETURN 'some files were not transferred (see log)'
      WHEN ARG(1) = 24 THEN RETURN 'some files vanished during the backup'
      WHEN ARG(1) = 30 | ARG(1) = 35 THEN RETURN 'timeout'
      OTHERWISE RETURN 'error'
   END

/* A line to the console and the log file. */
note: PROCEDURE EXPOSE logfile
   SAY ARG(1)
   IF OPEN('log', logfile, 'A') | OPEN('log', logfile, 'W') THEN DO
      CALL WRITELN('log', ARG(1))
      CALL CLOSE('log')
   END
   RETURN

finish:
   IF password ~= '' THEN ADDRESS COMMAND 'UnSetEnv RSYNC_PASSWORD'
   IF requester & summary ~= '' THEN
      ADDRESS COMMAND 'RequestChoice "Backup" "'summary'*NLog:' logfile'" "OK" >NIL:'
   RETURN

BREAK_C:
   CALL note 'Stopped with Ctrl-C.'
   summary = ''
   CALL finish
   EXIT 20
