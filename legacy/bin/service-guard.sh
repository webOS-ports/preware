#!/bin/sh

# Keeps the package manager service (org.webosinternals.ipkgservice) to the one instance
# its upstart job runs. Run as root by the preware2-service-guard upstart job every time
# that job (re)starts the service.
#
# The original Preware's D-Bus service file lets the hub start the service on demand.
# If anything calls it while the job's instance is down (the original Preware's install
# restarts it), the hub starts a second instance that takes the bus name. The job's
# instance then can't register, and since ipkgservice 1.9.19 it waits 5 seconds and
# exits, so upstart respawns it every 5 seconds for as long as the hub's instance lives.

SID="org.webosinternals.ipkgservice"
APPDIR=`dirname $0`/..
APPDIR=`cd $APPDIR && pwd`
SERVICE=/var/palm/system-services/${SID}.service

# Only upstart may start the service: with Type=static the hub holds calls until the
# job's instance has registered, instead of starting one of its own. The original
# Preware writes the file without it on every install.
if [ -f $SERVICE ] && ! grep -q '^Type=static' $SERVICE ; then
  cp $APPDIR/dbus/${SID}.service $SERVICE
  /usr/bin/ls-control scan-services || true
fi

# Let a call that is still being answered finish before looking for an instance of the hub.
sleep 20

HUBS=" `pidof ls-hubd` "
for P in `pidof ${SID}` ; do
  PARENT=`cut -d' ' -f4 /proc/$P/stat 2>/dev/null`
  case "$HUBS" in
    *" $PARENT "*) ;;
    *) continue ;;
  esac
  # Busy while it runs a command (ipkg, curl, a package script): try again on the job's next respawn
  if [ -n "`pgrep -P $P`" ] ; then
    continue
  fi
  kill $P
done

# Make sure the job is running: with Type=static nothing else starts the service.
case "`/sbin/initctl status ${SID} 2>&1`" in
  *"(start)"*) ;;
  *) /sbin/start ${SID} ;;
esac

exit 0
