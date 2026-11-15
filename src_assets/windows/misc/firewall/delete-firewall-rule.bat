@echo off

set RULE_NAME=Norishine

rem Delete the rule
netsh advfirewall firewall delete rule name=%RULE_NAME%
