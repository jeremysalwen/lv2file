#
# Regular cron jobs for the lv2file package
#
0 4	* * *	root	[ -x /usr/bin/lv2file_maintenance ] && /usr/bin/lv2file_maintenance
