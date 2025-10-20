GNOME Settings Daemon: Subscription Manager Plugin
==================================================

Testing:

To add a test acccount on subscription.rhsm.stage.redhat.com, use Ethel:
http://account-manager-stage.app.eng.rdu2.redhat.com/#view

Register with a username and password
-------------------------------------

    gdbus call \
     --session \
     --dest org.gnome.SettingsDaemon.Subscription \
     --object-path /org/gnome/SettingsDaemon/Subscription \
     --method org.gnome.SettingsDaemon.Subscription.Register "{'kind':<'username'>,'hostname':<'subscription.rhsm.stage.redhat.com'>,'username':<'rhughes_test'>,'password':<'barbaz'>}"

To register with a certificate
------------------------------

    gdbus call \
     --session \
     --dest org.gnome.SettingsDaemon.Subscription \
     --object-path /org/gnome/SettingsDaemon/Subscription \
     --method org.gnome.SettingsDaemon.Subscription.Register "{'kind':<'key'>,'hostname':<'subscription.rhsm.stage.redhat.com'>,'organisation':<'foo'>,'activation-key':<'barbaz'>}"

To unregister
-------------

    gdbus call \
     --session \
     --dest org.gnome.SettingsDaemon.Subscription \
     --object-path /org/gnome/SettingsDaemon/Subscription \
     --method org.gnome.SettingsDaemon.Subscription.Unregister

Debugging
---------

Get the UNIX socket using `Subscription.Register` then call something like:

    sudo G_MESSAGES_DEBUG=all ./plugins/subman/gsd-subman-helper \
     --address="unix:abstract=/var/run/dbus-ulGB1wfnbn,guid=71e6bf329d861ce366df7a1d5d036a5b" \
     --kind="register-with-username" \
     --username="rhughes_test" \
     --password="barbaz" \
     --hostname="subscription.rhsm.stage.redhat.com" \
     --organisation=""

You can all see some basic debugging running `rhsmd` in the foreground:

    sudo /usr/libexec/rhsmd -d -k

Known Limitations
=================

Proxy servers are not supported, nor are custom host ports or prefixes.
