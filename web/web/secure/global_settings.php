</script>
<?php
	/* try and make this page non cacheable */
	header("Expires: Mon, 26 Jul 1997 05:00:00 GMT");             // Date in the past
	header("Last-Modified: " . gmdate("D, d M Y H:i:s") . " GMT");// always modified
	header("Cache-Control: no-cache, must-revalidate");           // HTTP/1.1
	header("Pragma: no-cache");                                   // HTTP/1.0

	global $debug_level;
	global $prim;
	global $virtual_ip;
	global $fd;
	
	$ret_value ="";
	$flag = 0;
	$selected_host = "";	
	$selected = "";
	$server = "";
	global $virt;
	global $serv;
	require('parse.php'); /* read in the config! Hurragh! */
	$prim['service'] = "lvs";
	$prim['backup_active'] = "1";
	$temp = 0;
	if($selected =="") {
		$selected_host = 1;$selected=1;
		while($serv[$selected_host][$selected]['server'] != "") {
			if($serv[$selected_host][$selected]['server'] == "[unnamed]" && $serv[$selected_host][$selected]['address'] == "0.0.0.0") {
				$serv[$selected_host][$selected]['active']		=	2;
				$serv[$selected_host+1][$selected]['active']		=	2;
			}
			$selected++;
		}
	}
	open_file("w+");
	write_config("");
	$selected = "";

	$temp = explode(" ", $virt[1]['address']);
	if (empty($_GET['nat_nmask'])) {
		$NATRtrNmask	=	$prim['nat_nmask'];
	} else {
		$NATRtrNmask	=	$_GET['nat_nmask'];
	}
	if (isset($_GET['global_action']) && ($_GET['global_action'] == "SAVE")) {
		$address = $_GET['PriLVSIP'];
		$address = trim($address);
		$primary_ip = $_GET['PriPubIP'];
                $backup_ip = $_GET['backupIP'];
	//	$PrimOrBack = 0;
		$response[0][0] = 0;
		$count=0;
		$ip_address_matched=0;
		$command = "ip -f inet addr sh |grep 'inet' | grep -v '127.0.0.1' | grep -v 'LOOPBACK' | awk '{print $2}'";
		exec($command, $output[], $ret_value);
		while($output[0][$count] != NULL) {
			$ip_address2 = explode("/",$output[0][$count]);
			if($ip_address2[0] == $address) {
				$ip_address_matched=1;
			}
			if(($ip_address2[0] == $primary_ip) || ($ip_address2[0] == $backup_ip)) {
				//$PrimOrBack = 1;
			} 
			$count++;
		}
		if(!$ip_address_matched) {
			if($temp[0] != $address) {
				$cmd = "ping -c 10 $address |grep 'received' | awk '{print $4}'";
				exec($cmd, $response[], $ret_status);
			}
		}
// echo "response =";print_r($response);
		//if(!$$PrimOrBack) {
		if(0) {
                        echo "<script language=\"javascript\">alert(\"Primary or Backup Load Balancer server IP Address should consists of IP address of current machine\")</script>";
		}
		else {

			if($response[0][0] != 0 && (!$ip_address_matched)) {
				echo "<script language=\"javascript\">alert(\"Virtual IP ".$address." is used by another machine.\")</script>";
//				header("Location: global_settings.php");
//				exit;
//				return -1;
			}
			else {
/*				echo "vip=".$address;
				$intvip=sprintf("%u",ip2long($address));
				$primary_ip = $_GET['PriPubIP'];
				echo "pip=".$primary_ip;
				$intprimaryip=sprintf("%u",ip2long($primary_ip));
				$backup_ip = $_GET['backupIP'];
				echo "bip=".$backup_ip;
				$intbackupip=sprintf("%u",ip2long($backup_ip));
				$netmask = $_GET['vip_nmask'];
				echo "mask=".$netmask;
				$intnmask=sprintf("%u",ip2long($netmask));
*/
				$intvip=ip2long($address);
//				$primary_ip = $_GET['PriPubIP'];
				$intprimaryip=ip2long($primary_ip);
//				$backup_ip = $_GET['backupIP'];
				$intbackupip=ip2long($backup_ip);
				$netmask = $_GET['vip_nmask'];
				$intnmask=ip2long($netmask);

/*				echo "intvip=".$intvip;
				echo "intprimaryip=".$intprimaryip;
				echo "intbackupip=".$intbackupip;
				echo "intnmask=".$intnmask;
				echo "intvip and intnmask =".($intvip & $intnmask);
				echo "intprimaryip and intnmask =".($intprimaryip & $intnmask);
				echo "intbackupip and intnmask =".($intbackupip & $intnmask);
*/
				if(($intvip & $intnmask )!=($intprimaryip & $intnmask)) {
					echo "<script  language=\"javascript\">alert(\"Virtual and Primary IP address  should be in same subnet\")</script>";
//					return false;
				}
				else {
					if(($intvip & $intnmask )!=($intbackupip & $intnmask)) {
						echo "<script  language =\"javascript\">alert(\"Virtual and Backup IP address  should be in same subnet\")</script>";
//						return false;
					}	
					else {
						$dn=$_GET['deviceName'];
						$device = explode(" : ",$dn);
						$address = $address." ".$device[1].":1";
						$virt[1]['address']	 	= $address;
						$virt[2]['address']	 	= $address;	
						$temp = explode(" ", $address);
						$prim['primary']	= $_GET['PriPubIP'];
						$prim['backup']		= $_GET['backupIP'];
						$mask = $_GET['vip_nmask'];
						if($mask != "") {
							$virt[1]['vip_nmask'] = $mask;
							$virt[2]['vip_nmask'] = $mask;
						}
						$virt[1]['active'] = 1;
						$virt[2]['active'] = 1;
						open_file ("w+"); 
						write_config("");
						echo "<script  language=\"javascript\">alert(\"Cluster settings have been saved successfully. Press RELOAD SERVICE button to enable cluster services.\")</script>";
					}
				}	
			}
		}
		//include("http://$virtual_ip/fes-bin/HAsettings.cgi?type=4");
/*		else {
			echo "<script language=\"javascript\">alert(\"Virtual IP ".$address." is used by another machine.\")</script>";
		}
*/
//		echo $prim['primary'];
/*		if (empty($prim['primary_private'])) {
			$prim['backup_private'] = ""; 
                }
		
                $network = $prim['network'];
                switch ($network) {
                        case "NAT"			:	$network="nat"; break;
                        case "Direct Routing"		:	$network="direct"; break;
                        case "Tunneling"		:	$network="tunnel"; break;
                        default				:	break;
                }
      
                $prim['network']		=	$network;
        
                if ($prim['network'] == "nat") {
                        $prim['nat_router']	=	$_GET['NATRtrIP'] . " " . $_GET['NATRtrDev'];
                } else {
                        $prim['nat_router']	=	"";
                }

                if ($NATRtrNmask != "" ) {
                        $prim['nat_nmask']	=	$NATRtrNmask;
                } else {
                        $NATRtrNmask		=	"255.255.255.0";
		}*/
//		open_file ("w+"); 
//		write_config("");
	}

	if ($debug_level == "" ) {
		if ($prim['debug_level'] != "" ) {
			$debug_level		=	$prim['debug_level'];
		} else {
			$debug_level		=	"NONE";
			$prim['debug_level']	=	"NONE";
		}
		$prim['debug_level'] = $debug_level;
	};

	if ($prim['network'] == "") { $prim['network'] = "direct";}

	$network = "";
	if (isset($_GET['network'])) {
		$network = $_GET['network'];
	}
	if ($network == "NAT") { 
		$prim['network'] = "nat"; 
		if (isset($_GET['NATRtrIP']) && isset($_GET['NATRtrDev'])) {
			$prim['nat_router'] = $_GET['NATRtrIP'] . " " . $_GET['NATRtrDev']; 
		}
	}
	if ($network == "Direct Routing") { 
	    $prim['network'] = "direct"; 
	    $prim['nat_router'] = ""; 
	}
	if ($network == "Tunneling") { 
	    $prim['network'] = "tunnel"; 
	    $prim['nat_router'] = ""; 
	}
	/* Make a semi sensible guess */
	if ($prim['primary'] == "") {
		$prim['primary'] = $_SERVER['SERVER_ADDR'];
		$PriLVSIP = $_SERVER['SERVER_ADDR'];
	}

	// echo "Query = $QUERY_STRING";

	
	if (isset($_GET['selected_host'])) {
		$selected_host = $_GET['selected_host'];
//		echo "Selected_host".$selected_host;
	}

	if (isset($_GET['selected'])) {
		$selected = $_GET['selected'];
	}

//	if ((isset($_GET['real_service'])) && ($_GET['real_service'] == "CANCEL")) {
		/* Redirect browser to editing page */
//		header("Location: redundancy.php");
		/* Make sure that code below does not get executed when we redirect. */
//		exit;
//	}

/*	$real = $_GET['real_service'];
	echo $real;
	switch($real) {
		case "ADD":
			add_service($selected_host);
			add_service($selected_host+1);
			$selected = $server;
			header("Location: virtual_edit_real_edit.php?selected_host=$selected_host&selected=$server");
			exit;	
		case "DELETE":
			echo "About to delete entry number" .$selected_host ."<BR>" >> "/tmp/logpiranha"; 
			$selected_host = 1;
			open_file("w+");
			$level = 2;
			write_config($level,$selected_host , $selected);
			$selected_host = $selected_host + 1;
			echo "sh ".$selected_host;
			echo "se ".$selected;
			open_file("w+");
			write_config($level, $selected_host, $selected);
			open_file("w+");
			write_config("");
			header("Location: global_settings.php");
			exit;
		case "EDIT":
			header("Location: virtual_edit_real_edit.php?selected_host=$selected_host&selected=$selected");
			exit;
	}
*/
	/* Some magic used to allow the edit command to pull up another web page */
	if ((isset($_GET['real_service'])) && ($_GET['real_service'] == "Edit")) {
		/* Redirect browser to editing page */
		if($selected == "") {
			echo "<script language=\"javascript\">alert(\"There are no HySecure servers configured.\")</script>";
//			echo "<a href=\"global_settings.php\">Back</a>";
//			header("Location: global_settings.php");
//			exit;
		}
		else {
			header("Location: virtual_edit_real_edit.php?selected_host=$selected_host&selected=$selected");
			exit;
		}
		/* Make sure that code below does not get executed when we redirect. */
	}
	if (($_GET['real_service'] != '') && ($_GET['real_service'] == "Add")) {
/*		if( $selected_host == "")
			$selected_host = 1;
		else 
			$selected_host ++;*/
		add_service($selected_host);
		add_service($selected_host+1);
		$selected = $server;
//		echo "server  =".$server;
		header("Location: virtual_edit_real_edit.php?selected_host=$selected_host&selected=$server");
		exit;
//		add_service($selected_host+2);
//		open_file("w+");
//		write_config("");
	}
	if ((isset($_GET['real_service'])) && ($_GET['real_service'] == "Delete")) {
		if($selected == "") {
			echo "<script language=\"javascript\">alert(\"There are no HySecure servers configured.\")</script>";
//			echo "<a href=\"global_settings.php\">Back</a>";
//			header("Location: global_settings.php");
//			exit;
		}
		else {
//			$ret = "<script language=\"javascript\">deletion()</script>";
//			echo "return = ".$ret;
//			if($ret_val == 0) {
//				echo "return = ".$ret_val;
				echo "About to delete entry number" .$selected_host ."<BR>" >> "/tmp/logpiranha"; 
				$selected_host = 1;
				open_file("w+");
				$level = 2;
				write_config($level,$selected_host , $selected);
				$selected_host = $selected_host + 1;
				open_file("w+");
				write_config($level, $selected_host, $selected);
				open_file("w+");
				write_config("");
				header("Location: global_settings.php");
				exit;
//			}

		}
	}

	if ((isset($_GET['real_service'])) && (($_GET['real_service'] == "ADVANCED HA CONFIGURATION") || $_GET['real_service'] == "ADVANCE")) {
		header("Location: redundancy.php");
		/* Make sure that code below does not get executed when we redirect. */
		exit;
	}
	if ((isset($_GET['reload_service'])) && (($_GET['reload_service'] == "RELOAD SERVICE") || $_GET['reload_service'] == "RELOAD")) {
		exec("/home/fes/SyncBinary PulseRst",$interfaces[],$ret_val);
               	echo "<script language=\"javascript\">alert(\"Cluster Services reloaded successfully. You can monitor services status from Hysecure DashBoard.\")</script>";
	}

	/* Umm,... just in case someone is dumb enuf to fiddle */
	if (empty($selected_host)) { $selected_host=1; /*echo "sh in call= ".$selected_host;*/ }
/*	function interface()
	{
		$inter = $_GET['deviceName'];
		exec("ip addr sh | grep ': ' | grep -v 'LOOPBACK' | awk '{print $2}'",&$interfaces[],&$ret_val);
		$i=0;
		while($interfaces[0][$i] != NULL) {
		echo "<OPTION selected=";
		if($inter == $interfaces[0][$i]) {echo "selected";}echo " value=";
 		echo $interfaces[0][$i];
	 	echo ">";echo $interfaces[0][$i]; $i++;}
	}
*/
?>
<HTML>
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML Strict Level 3//EN">

	<HEAD>
	<TITLE>Propalms (Configuration Settings)</TITLE>
	<link href = "appearance.css" rel = "stylesheet" type = "text/css" />
	<link rel="StyleSheet" href="help.css" type="text/css"/>
	<script type="text/javascript" language="javascript" src="functions.js"></script>
<script type="text/javascript">
function deletion()
{
	if(confirm("Are you sure?You want to delete server")) {
		document.getElementById('return_value').value = 1;
		return true;
	}
	else {
		document.getElementById('return_value').value = 0;
		return false;
	}
}
function check()
{
if(!configure(document.getElementById("configuration"))) {
		return false;
	}
<?php
	open_file ("w+"); 
	write_config("");
?>
	document.configuration.submit();
}
</script>
<!--<HTML>
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML Strict Level 3//EN">

	<HEAD>
	<TITLE>Propalms (Configuration Settings)</TITLE>
	<link href = "appearance.css" rel = "stylesheet" type = "text/css" />
	<script type="text/javascript" language="javascript" src="functions.js"></script>
<script type="text/javascript">
function display()
{
	alert("There are no HySecure servers configured.");
}
function check()
{
//	hello(document.configuration);
	if(!configure(document.getElementById("configuration"))) {
//		alert("stopped proceesing");
		return false;
	}

<?php
//	open_file ("w+"); 
//	write_config("");
?>

	document.configuration.submit();
}
</script>
-->
<!--
<STYLE TYPE="text/css">
 

TD      {
        font-family: helvetica, sans-serif;
        }
        
.logo   {
        color: #FFFFFF;
        }
        
A.logolink      {
        color: #FFFFFF;
        font-size: .8em;
        }
        
.taboff {
        color: #FFFFFF;
        }
        
.tabon  {
        color: #999999;
        }
        
.title  {
        font-size: .8em;
        font-weight: bold;
        color: #660000;
        }
        
.smtext {
        font-size: .8em;
        }
        
.green  {
        color: 

</STYLE>
-->
	</HEAD>

	<BODY>
<table width="99%">
<tr valign="top"><td>
<!--
<TABLE WIDTH="100%" BORDER="0" CELLSPACING="0" CELLPADDING="5">
	<TR BGCOLOR="#CC0000"> <TD CLASS="logo"> <B>PIRANHA</B> CONFIGURATION TOOL </TD>
	<TD ALIGN=right CLASS="logo">
           <A HREF="introduction.html" CLASS="logolink">
           INTRODUCTION</A> | <A HREF="help.php" CLASS="logolink">
           HELP</A></TD>
	</TR>
</TABLE>
--><div name ="hi"></div>
<TABLE WIDTH = "99%">
  				<TR><TD width="80%"><DIV CLASS = "HeaderDefault"><DIV CLASS="HeaderText">Configuration</DIV></DIV></TD></TR>
</TABLE>
<TABLE WIDTH = "99%">
			<TR><TD CLASS = "TdLine"><HR CLASS = "Line"></TD></TR>
</TABLE>
<TABLE WIDTH = "99%">
<TR><TD width="80%" valign="top">
<FORM NAME="configuration" METHOD="GET" ENCTYPE="application/x-www-form-urlencoded" ACTION="global_settings.php">
	<TABLE STYLE = "width:550px">
			<TR><TD>
				<FIELDSET CLASS = "LB">
					<TABLE  BORDER="0" CELLSPACING="1" CELLPADDING="5">
						<TR><TD CLASS="title" COLSPAN="2"><STRONG>Environment</STRONG></TD></TR>
						<TR><TD>Virtual IP Address:</TD><TD><INPUT TYPE="TEXT" NAME="PriLVSIP"  CLASS = "Text" VALUE="<?php echo $temp[0] ?>"></TD></TR>
						<TR><TD>Virtual IP Network Mask Address:</TD><TD>
								<INPUT TYPE="TEXT" NAME="vip_nmask"  CLASS = "Text" VALUE="<?php echo $virt[$selected_host]['vip_nmask'] ?>">
						</TD></TR>
						<TR><TD>Primary Load Balancer server IP Address:</TD><TD><INPUT TYPE="TEXT" NAME="PriPubIP" CLASS = "Text" VALUE="<?php echo $prim['primary'] ?>"></TD></TR>
						<tr><td>Backup Load Balancer server IP Address:</td><td><input type="text" name="backupIP" class = "Text" value="<?php echo $prim['backup']; ?>"></td></tr>
						<tr><td>Load Balanced Port No. (Separated with comma):</td><td><input type="text" name="portNo" class = "Text" disabled="disabled" value="<?php echo "80,443"?>"></td></tr>
						<tr><td>Device Name (currently selected device: <?php $disp = explode(":",$temp[1]);
echo $disp[0];
?>):
</td><td><select name="deviceName" class = "Select">
<?php exec("ip -f inet addr sh | awk '{if($1==\"inet\"){ k=split($0,array1,\" \"); if (array1[k] != \"lo\"){if(array1[k-1]!=\"secondary\"){j=split($2,array2,\"/\"); print array2[1] \" : \" array1[k]; }}}}'",$interfaces[],$ret_val);
	$i=0;
	while($interfaces[0][$i] != NULL) {
?>
	<OPTION value="<?php echo $interfaces[0][$i]; ?>" <?php $dname=explode(" : ",$interfaces[0][$i]);if(trim($disp[0]) == trim($dname[1])) echo "SELECTED";?>>
<?php
	echo $interfaces[0][$i]; $i++;}?></select></td></tr>
	<TR><TD><!--<BUTTON TYPE="SUBMIT" NAME="global_action" CLASS = "ButtonNormalAppearance" VALUE="SAVE" onclick = "return check()">SAVE</BUTTON></TD>
	<TD>--><BUTTON TYPE="SUBMIT" STYLE = "font-weight:bold;WIDTH:200PX;font-size:10px" NAME="real_service" CLASS = "ButtonNormalAppearance" VALUE="ADVANCE">ADVANCED HA CONFIGURATION</BUTTON></TD>
	<TD><BUTTON TYPE="SUBMIT" NAME="global_action" CLASS = "ButtonNormalAppearance" VALUE="SAVE" onclick = "return check()">SAVE</BUTTON></TD>
<!--	<TD><a href="http://administration/fes-bin/HAsettings.cgi?type=4">RELOAD SERVICE</a></TD></TR>-->
	<TD><button name="reload_service" style="width:100px;" TYPE="SUBMIT" CLASS = "ButtonNormalAppearance" value="RELOAD">RELOAD SERVICE</button></TD></TR>
<?php// echo $interface;

//print_r($interfaces);?>
     <?php// if ($prim['service'] != "fos") { ?>
	<!--<TR>
		<TD>Use network type:<BR>(Current type is: <B><?php echo $prim['network']; ?></B> ) </TD>
		<TD>
		<SELECT NAME="network">
				<OPTION <?php// if ($prim['network'] == "nat") { echo "SELECTED"; } ?>> NAT
				<OPTION <?php// if ($prim['network'] == "direct") { echo "SELECTED"; } ?>> Direct Routing
				<OPTION <?php// if ($prim['network'] == "tunnel") { echo "SELECTED"; } ?>> Tunneling
		</SELECT>
		</TD>
		<TD>
<table><tr><td>	<BUTTON TYPE="SUBMIT" NAME="network" CLASS = "ButtonNormalAppearance" VALUE="NAT">NAT</BUTTON></td><td>
		<BUTTON TYPE="SUBMIT" STYLE = "width: 100px" NAME="network" CLASS = "ButtonNormalAppearance" VALUE="Direct Routing">Direct Routing</BUTTON></td><td>
		<BUTTON TYPE="SUBMIT" NAME="network" CLASS = "ButtonNormalAppearance" VALUE="Tunneling">Tunneling</BUTTON></td></tr></table>
	</TR> 
	<?php// if ($prim['network'] == "nat" ) { ?>
	 <TR>
		<TD>NAT Router IP:</TD>
		<TD><INPUT TYPE="TEXT" NAME="NATRtrIP" CLASS = "Text" VALUE="<?php
	//		$ip = explode(" ", $prim['nat_router']);
	//		echo $ip[0];
			// echo $prim['...???? WHAT??
		?>"></TD>
	</TR>
	<TR>
		<TD>NAT Router netmask:</TD>
		<TD>
			<SELECT NAME="nat_nmask" CLASS = "Select">
				<OPTION <?php// if ($NATRtrNmask == "255.255.255.255") { echo "SELECTED"; } ?>> 255.255.255.255
				<OPTION <?php// if ($NATRtrNmask == "255.255.255.252") { echo "SELECTED"; } ?>> 255.255.255.252
				<OPTION <?php// if ($NATRtrNmask == "255.255.255.248") { echo "SELECTED"; } ?>> 255.255.255.248
				<OPTION <?php// if ($NATRtrNmask == "255.255.255.240") { echo "SELECTED"; } ?>> 255.255.255.240
				<OPTION <?php//  if ($NATRtrNmask == "255.255.255.224") { echo "SELECTED"; } ?>> 255.255.255.224
				<OPTION <?php// if ($NATRtrNmask == "255.255.255.192") { echo "SELECTED"; } ?>> 255.255.255.192
				<OPTION <?php//  if ($NATRtrNmask == "255.255.255.128") { echo "SELECTED"; } ?>> 255.255.255.128
				<OPTION <?php//  if ($NATRtrNmask == "255.255.255.0")   { echo "SELECTED"; } ?>> 255.255.255.0
				<OPTION <?php//  if ($NATRtrNmask == "255.255.252.0")   { echo "SELECTED"; } ?>> 255.255.252.0
				<OPTION <?php//  if ($NATRtrNmask == "255.255.248.0")   { echo "SELECTED"; } ?>> 255.255.248.0
				<OPTION <?php//  if ($NATRtrNmask == "255.255.240.0")   { echo "SELECTED"; } ?>> 255.255.240.0
				<OPTION <?php//  if ($NATRtrNmask == "255.255.224.0")   { echo "SELECTED"; } ?>> 255.255.224.0
				<OPTION <?php//  if ($NATRtrNmask == "255.255.192.0")   { echo "SELECTED"; } ?>> 255.255.192.0
				<OPTION <?php//  if ($NATRtrNmask == "255.255.128.0")   { echo "SELECTED"; } ?>> 255.255.128.0
				<OPTION <?php//  if ($NATRtrNmask == "255.255.0.0")     { echo "SELECTED"; } ?>> 255.255.0.0
			</SELECT>
		</TD>
	</TR>
	<TR>
		<TD>NAT Router device:</TD>
		<TD><INPUT TYPE="TEXT" NAME="NATRtrDev" CLASS = "Text" VALUE="<?php
	// 		$dev = explode(" ", $prim['nat_router']);
		// 	if (isset($dev[1]))
			// 	echo $dev[1];
			// echo $prim['..??
	?>"></TD>
	</TR>-->
	<?php//  } ?>
	<?php//  } ?>
					</TABLE>
				</FIELDSET>
				</TD></TR>
				<TR><TD>
				</TD></TR>
				<TR><TD>
<?php// echo "selected_host = ".$selected_host; ?>
				<FIELDSET CLASS = "LB">
     					<TABLE WIDTH = "500px">
      						<TR><TD CLASS="title" COLSPAN="2"><STRONG>HySecure Servers</STRONG></TD></TR>
      						<TR><TD>
        						<TABLE CLASS="TableDefault">
         							<TR>
          								<TH class="TableHeadColumnDefault" align = "middle">&nbsp;</TH>
									<!--<TD style = "width:100px" CLASS="title" align = "middle"><STRONG>Status</STRONG></TD>-->
          								<TH class="TableHeadColumnDefault" align = "middle"><STRONG>HySecure Server Name</STRONG></TH>
          								<TH class="TableHeadColumnDefault" align = "middle"><STRONG>IP Address</STRONG></TH>
<?php //	<TD CLASS="title">NETMASK</TD> ?>
								</TR>
							       
<!-- Somehow dynamically generated here -->
	

	<?php
	/* magic */
//echo "selected_host = ".$selected_host;

	echo "<INPUT TYPE=HIDDEN NAME=virtual VALUE=$selected_host>";
//echo// "sh=".$selected_host;
	$loop=1;
//echo "server=".$serv[$selected_host][$loop]['server'];
//echo// "server_value=".$serv[$selected_host][$loop]['server'];
	while ((isset($serv[$selected_host][$loop]['server'])) && ($serv[$selected_host][$loop]['server'] != "" )) {
	if($serv[$selected_host][$loop]['server'] == "[unnamed]" && $serv[$selected_host][$loop]['address'] == "0.0.0.0") { continue;}

//	echo "hi";
		echo "<TR>";
		echo "<TD class="."TableTdDefault"."><INPUT TYPE=RADIO NAME=selected VALUE=" . $loop; if ($selected == "" ) { $selected = 1; }; if ($loop == $selected) { echo " CHECKED"; }; echo "></TD>";
/*		echo "<TD><INPUT TYPE=HIDDEN NAME=active COLS=6 VALUE=";
			switch ($serv[$selected_host][$loop]['active']) {
				case "0"	:	echo "down><FONT COLOR=red>Down</FONT>";	break;
				case "1"	:	echo "up><FONT COLOR=blue>up</FONT>";		break;
				case "2"	:	echo "Active><FONT COLOR=green></FONT>";	break;
				default		:	$serv[$selected_host][$loop]['active'] = "0";
							echo "down><FONT COLOR=red></FONT>";		break;
			}
				
		echo "</TD>";*/
		echo "<TD class="."TableTdDefault"."><INPUT TYPE=HIDDEN NAME=name COLS=6 VALUE=";		echo $serv[$selected_host][$loop]['server']	. ">";
		echo $serv[$selected_host][$loop]['server']	. "</TD>";
		echo "<TD class="."TableTdDefault"."><INPUT TYPE=HIDDEN NAME=address COLS=10 VALUE=";	echo $serv[$selected_host][$loop]['address']	. ">";
		echo $serv[$selected_host][$loop]['address']	. "</TD>";
/*		if (empty($serv[$selected_host][$loop]['nmask']) || $serv[$selected_host][$loop]['nmask'] == "0.0.0.0") {
			echo "<TD>Unused</TD>";
		} else {
			echo "<TD><INPUT TYPE=HIDDEN NAME=netmask COLS=10 VALUE=";	echo $serv[$selected_host][$loop]['nmask']	. ">";
			echo $serv[$selected_host][$loop]['nmask']	. "</TD>";

			echo "<TD>Unused</TD>";
		} else {
			echo "<TD><INPUT TYPE=HIDDEN NAME=netmask COLS=10 VALUE=";	echo $serv[$selected_host][$loop]['nmask']	. ">";
			echo $serv[$selected_host][$loop]['nmask']	. "</TD>";
		}*/
		echo "</TR>";
	
	$loop++;
	}
	$server = $loop-1;
	echo "</TABLE>";
//	echo "server =".$server;
//	$selected_host = $loop-1;
	?>
	

<!-- end of dynamic generation -->



<!-- should align beside the above table -->
						</TD></TR>
        					<TR><TD>
							<BUTTON STYLE = "WIDTH:90px;margin-right:10px" TYPE="SUBMIT" NAME="real_service" CLASS = "ButtonNormalAppearance" VALUE="Add">Add</BUTTON>
         						<BUTTON STYLE = "WIDTH:90px;margin-right:10px" TYPE="SUBMIT" NAME="real_service" CLASS = "ButtonNormalAppearance" VALUE="Delete">Delete</BUTTON>
         						<BUTTON STYLE = "WIDTH:90px;" TYPE="SUBMIT" NAME="real_service" CLASS = "ButtonNormalAppearance"  VALUE="Edit">Edit</BUTTON>
         					<!--	<BUTTON STYLE = "WIDTH:90px" TYPE="SUBMIT" NAME="real_service" CLASS = "ButtonNormalAppearance"  VALUE="(DE)ACTIVATE">(DE)ACTIVATE</BUTTON>-->
						</TD></TR>
						<TR><TD><DIV name = "errMsg"></DIV></TD></TR>
					</TABLE>
				</FIELDSET>
<?php echo "<INPUT TYPE=HIDDEN NAME=selected_host VALUE=$selected_host>" ?>
<?php echo "<label TYPE=HIDDEN NAME=return_value VALUE=$ret_value>" ?>

<?php open_file ("w+"); write_config(""); ?>
     				</TD></TR>
				<TR><TD>
<?php
	//sleep(30);
//	echo "Process click"; 
//	open_file ("w+"); 
//	echo "Process write "; 
//	write_config(""); 
//	echo "Process done"
	#echo $prim['primary']; 
?>
				</TD></TR>
			</TABLE>
		</FORM>
	</td></tr></table>
</TD>
<TD width="20%" class="td_background" style="padding-left:10px" valign="top">

    <div class="help_detail">
	<br />
		<font class="help_desc">Configuration</font>
	<br />
	<br />

	        <font class="help_item">Environment</font>
        <br />Allows to configure Load Balancer information.
        <br />        
        <br />
        
		<font class="help_item">Virtual IP Address</font>
        <br />Virtual IP Address for Load Balancing Service.
        <br />
        <br />
        
		<font class="help_item">Virtual IP NetMask Address</font>
        <br />NetMask Address for Load Balancing Service.
        <br />
        <br />
        
		<font class="help_item">Primary Load Balancer server IP Address</font>
        <br />IP Address of Primary Load Balancer server.
        <br />
        <br />
		<font class="help_item">Backup Load Balancer server IP Address</font>
        <br />IP Address of Backup Load Balancer server.
        <br />
        <br />
		<font class="help_item">Load Balanced Port No.</font>
        <br />Port no.(s) used for Load Balancing service.
        <br />
        <br />
		<font class="help_item">Device Name</font>
        <br />Interface name on the Gateway used for Load Balancing service(currently selected interface name is shown here).
        <br />
        <br />
		<font class="help_item">Advanced HA Configuration</font>
        <br />Advanced High Availability service settings are configured by "Advanced HA Configuration".
        <br />
        <br />
        
		<font class="help_item">HySecure Servers</font>
        <br />Manipulation of HySecure Servers in HySecure Cluster.
        <br />
        <br />
		<font class="help_item">Add</font>
        <br />To Add HySecure Server in Cluster.
        <br />
        <br />
		<font class="help_item">Delete</font>
        <br />To Delete HySecure Server from Cluster.
        <br />
        <br />
		<font class="help_item">Edit</font>
        <br />To Edit HySecure Server details.
        <br />
        <br />

    </div>
</TD></TR>
</TABLE>
	<script>
//		parent.help.location.href="http://192.168.1.181:3636/secure/HAAdvance.html";
	</script>
		</TD></TR>
		</TABLE>
	</BODY>
</HTML>
