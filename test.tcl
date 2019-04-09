#!/usr/local/bin/tclsh

package require Tcl 8.6
#package require Syslog
package require tclnetsnmp 3.0

#syslog -ident foo -options {PERROR PID}


proc main {} {
	global argv

	netsnmp::read_module Q-BRIDGE-MIB

	set name Q-BRIDGE-MIB::dot1qVlanStaticEgressPorts.1
	puts "[netsnmp::name $name]"
	exit 0

	#set sd [netsnmp::session -v 2c -c public bar-prirechnaja-5-12dl5]
	set sd [netsnmp::session -v 2c -c public ns-bar201br]
	puts $sd

	#set rd [::netsnmp::get $sd [list IF-MIB::ifAlias.1 1.2.3.4.5.6.7 IF-MIB::ifAdminStatus.1 \
	#		LLDP-MIB::lldpStatsRemTablesAgeouts.0 Q-BRIDGE-MIB::dot1qVlanStaticEgressPorts.1]]
	#set rd [::netsnmp::get $sd [list IF-MIB::ifAlias.1 IF-MIB::ifAdminStatus.1 \
	#		LLDP-MIB::lldpStatsRemTablesAgeouts.0 Q-BRIDGE-MIB::dot1qVlanStaticEgressPorts.1]]
	#netsnmp_dump_result $rd rd

	#set rd [::netsnmp::bulkget $sd [list IF-MIB::ifAlias.1 1.2.3.4.5.6.7 IF-MIB::ifAdminStatus.1 \
	#		LLDP-MIB::lldpStatsRemTablesAgeouts.0 Q-BRIDGE-MIB::dot1qVlanStaticEgressPorts.1]]
	#set rd [::netsnmp::bulkget $sd [list IF-MIB::ifAlias.1 IF-MIB::ifAdminStatus.1 \
	#		LLDP-MIB::lldpStatsRemTablesAgeouts.0 Q-BRIDGE-MIB::dot1qVlanStaticEgressPorts.1]]
	#netsnmp_dump_result $rd rd

	#set rd [::netsnmp::walk $sd IF-MIB::ifName.1]
	#netsnmp_dump_result $rd rd

	#set rd [::netsnmp::bulkwalk $sd [list IF-MIB::ifName]]
	#netsnmp_dump_result $rd rd

	set rd [::netsnmp::bulkwalk_ex $sd [list IF-MIB::ifName IF-MIB::ifType]]
	netsnmp_dump_result $rd rd
}
main


#set res [netsnmp_get $sd \
#		IF-MIB::ifAlias.1 \
#		1.2.3.4.5.6.7 \
#		IF-MIB::ifAdminStatus.1 \
#		LLDP-MIB::lldpStatsRemTablesAgeouts.0 \
#		Q-BRIDGE-MIB::dot1qVlanStaticEgressPorts.1]

#set res [netsnmp_bulkget $sd -n 2 -r 128 IF-MIB::ifAlias.1 IF-MIB::ifAdminStatus.1 \
#		Q-BRIDGE-MIB::dot1qVlanStaticEgressPorts.1 \
#		Q-BRIDGE-MIB::dot1qVlanForbiddenEgressPorts.1 \
#		Q-BRIDGE-MIB::dot1qVlanStaticUntaggedPorts.1]
#
#set res [netsnmp_walk $sd Q-BRIDGE-MIB::dot1qVlanStaticTable]
#set res [netsnmp_walk $sd Q-BRIDGE-MIB::qBridgeMIB]
#set res [netsnmp_bulkwalk $sd -max-repetitions 128 Q-BRIDGE-MIB::qBridgeMIB]
#set res [netsnmp_bulkwalk_ex $sd -max-repetitions 10 IP-FORWARD-MIB::ipForward]
#set res [netsnmp_bulkwalk_ex $sd -max-repetitions 10 BRIDGE-MIB::dot1dTpFdbAddress BRIDGE-MIB::dot1dTpFdbPort]
#set res [netsnmp_bulkwalk_ex $sd -max-repetitions 10 Q-BRIDGE-MIB::dot1dTpFdbTable]
#set res [netsnmp_get $sd SNMPv2-MIB::sysContact.0]
