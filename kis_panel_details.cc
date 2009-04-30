/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "config.h"

// Panel has to be here to pass configure, so just test these
#if (defined(HAVE_LIBNCURSES) || defined (HAVE_LIBCURSES))

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include <sstream>
#include <iomanip>

#include "kis_panel_widgets.h"
#include "kis_panel_frontend.h"
#include "kis_panel_windows.h"
#include "kis_panel_preferences.h"
#include "kis_panel_details.h"

int NetDetailsButtonCB(COMPONENT_CALLBACK_PARMS) {
	((Kis_NetDetails_Panel *) aux)->ButtonAction(component);
	return 1;
}

int NetDetailsMenuCB(COMPONENT_CALLBACK_PARMS) {
	((Kis_NetDetails_Panel *) aux)->MenuAction(status);
	return 1;
}

int NetDetailsGraphEvent(TIMEEVENT_PARMS) {
	return ((Kis_NetDetails_Panel *) parm)->GraphTimer();
}

Kis_NetDetails_Panel::Kis_NetDetails_Panel(GlobalRegistry *in_globalreg, 
									 KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	grapheventid = 
		globalreg->timetracker->RegisterTimer(SERVER_TIMESLICES_SEC, NULL, 1,
											  &NetDetailsGraphEvent, (void *) this);

	menu = new Kis_Menu(globalreg, this);

	menu->SetCallback(COMPONENT_CBTYPE_ACTIVATED, NetDetailsMenuCB, this);

	mn_network = menu->AddMenu("Network", 0);
	mi_nextnet = menu->AddMenuItem("Next network", mn_network, 'n');
	mi_prevnet = menu->AddMenuItem("Prev network", mn_network, 'p');
	menu->AddMenuItem("-", mn_network, 0);
	mi_close = menu->AddMenuItem("Close window", mn_network, 'w');

	mn_view = menu->AddMenu("View", 0);
	mi_net = menu->AddMenuItem("Network Details", mn_view, 'n');
	mi_clients = menu->AddMenuItem("Clients", mn_view, 'c');
	menu->AddMenuItem("-", mn_view, 0);
	mi_graphsig = menu->AddMenuItem("Signal Level", mn_view, 's');
	mi_graphpacket = menu->AddMenuItem("Packet Rate", mn_view, 'p');
	mi_graphretry = menu->AddMenuItem("Retry Rate", mn_view, 'r');

	menu->Show();
	AddComponentVec(menu, KIS_PANEL_COMP_EVT);

	// Details scroll list doesn't get the current one highlighted and
	// doesn't draw titles, also lock to fit inside the window
	netdetails = new Kis_Scrollable_Table(globalreg, this);
	netdetails->SetHighlightSelected(0);
	netdetails->SetLockScrollTop(1);
	netdetails->SetDrawTitles(0);
	AddComponentVec(netdetails, (KIS_PANEL_COMP_DRAW | KIS_PANEL_COMP_EVT |
								 KIS_PANEL_COMP_TAB));

	// We need to populate the titles even if we don't use them so that
	// the row handler knows how to draw them
	vector<Kis_Scrollable_Table::title_data> titles;
	Kis_Scrollable_Table::title_data t;
	t.width = 12;
	t.title = "field";
	t.alignment = 2;
	titles.push_back(t);
	t.width = 0;
	t.title = "value";
	t.alignment = 0;
	titles.push_back(t);

	netdetails->AddTitles(titles);

	netdetails->Show();

	siggraph = new Kis_IntGraph(globalreg, this);
	siggraph->SetName("DETAIL_SIG");
	siggraph->SetPreferredSize(0, 8);
	siggraph->SetScale(-110, -40);
	siggraph->SetInterpolation(1);
	siggraph->SetMode(0);
	siggraph->Show();
	siggraph->AddExtDataVec("Signal", 4, "graph_detail_sig", "yellow,yellow", 
		 					  ' ', ' ', 1, &sigpoints);
	AddComponentVec(siggraph, KIS_PANEL_COMP_EVT);

	packetgraph = new Kis_IntGraph(globalreg, this);
	packetgraph->SetName("DETAIL_PPS");
	packetgraph->SetPreferredSize(0, 8);
	packetgraph->SetScale(0, 0);
	packetgraph->SetInterpolation(1);
	packetgraph->SetMode(0);
	packetgraph->Show();
	packetgraph->AddExtDataVec("Packet Rate", 4, "graph_detail_pps", "green,green", 
							  ' ', ' ', 1, &packetpps);
	AddComponentVec(packetgraph, KIS_PANEL_COMP_EVT);

	retrygraph = new Kis_IntGraph(globalreg, this);
	retrygraph->SetName("DETAIL_RETRY_PPS");
	retrygraph->SetPreferredSize(0, 8);
	retrygraph->SetScale(0, 0);
	retrygraph->SetInterpolation(1);
	retrygraph->SetMode(0);
	retrygraph->Show();
	retrygraph->AddExtDataVec("Retry Rate", 4, "graph_detail_retrypps", "red,red", 
							  ' ', ' ', 1, &retrypps);
	AddComponentVec(retrygraph, KIS_PANEL_COMP_EVT);

	ClearGraphVectors();

	SetTitle("");

	vbox = new Kis_Panel_Packbox(globalreg, this);
	vbox->SetPackV();
	vbox->SetHomogenous(0);
	vbox->SetSpacing(0);
	vbox->Show();

	vbox->Pack_End(siggraph, 0, 0);
	vbox->Pack_End(packetgraph, 0, 0);
	vbox->Pack_End(retrygraph, 0, 0);

	vbox->Pack_End(netdetails, 1, 0);

	AddComponentVec(vbox, KIS_PANEL_COMP_DRAW);

	last_dirty = 0;
	last_mac = mac_addr(0);
	dng = NULL;

	vector<string> td;
	td.push_back("");
	td.push_back("No network selected / Empty network selected");
	netdetails->AddRow(0, td);

	UpdateViewMenu(-1);

	SetActiveComponent(netdetails);

	main_component = vbox;

	Position(WIN_CENTER(LINES, COLS));
}

Kis_NetDetails_Panel::~Kis_NetDetails_Panel() {
	if (grapheventid >= 0 && globalreg != NULL)
		globalreg->timetracker->RemoveTimer(grapheventid);
}

void Kis_NetDetails_Panel::ClearGraphVectors() {
	lastpackets = 0;
	sigpoints.clear();
	packetpps.clear();
	retrypps.clear();
	for (unsigned int x = 0; x < 120; x++) {
		sigpoints.push_back(-256);
		packetpps.push_back(0);
		retrypps.push_back(0);
	}
}

void Kis_NetDetails_Panel::UpdateGraphVectors(int signal, int pps, int retry) {
	sigpoints.push_back(signal);
	if (sigpoints.size() > 120)
		sigpoints.erase(sigpoints.begin(), sigpoints.begin() + sigpoints.size() - 120);

	if (lastpackets == 0)
		lastpackets = pps;
	packetpps.push_back(pps - lastpackets);
	lastpackets = pps;
	if (packetpps.size() > 120)
		packetpps.erase(packetpps.begin(), packetpps.begin() + packetpps.size() - 120);

	retrypps.push_back(retry);
	if (retrypps.size() > 120)
		retrypps.erase(retrypps.begin(), retrypps.begin() + retrypps.size() - 120);
}

int Kis_NetDetails_Panel::AppendSSIDInfo(int k, Netracker::tracked_network *net,
										 Netracker::adv_ssid_data *ssid) {
	ostringstream osstr;
	vector<string> td;
	td.push_back("");
	td.push_back("");

	if (ssid != NULL) {
		td[0] = "SSID:";
		td[1] = ssid->ssid;
		netdetails->AddRow(k++, td);

		if (ssid->ssid_cloaked) {
			td[0] = "";
			td[1] = "(Cloaked)";
			netdetails->AddRow(k++, td);
		}

		td[0] = "SSID Len:";
		td[1] = IntToString(ssid->ssid.length());
		netdetails->AddRow(k++, td);

		if (ssid->dot11d_vec.size() > 0) {
			td[0] = "Country:";
			td[1] = ssid->dot11d_country;
			netdetails->AddRow(k++, td);

			td[0] = "";
			for (unsigned int z = 0; z < ssid->dot11d_vec.size(); z++) {
				td[1] = string("Channel ") + 
					IntToString(ssid->dot11d_vec[z].startchan) +
					string("-") +
					IntToString(ssid->dot11d_vec[z].startchan +
								ssid->dot11d_vec[z].numchan) +
					string(" ") +
					IntToString(ssid->dot11d_vec[z].txpower) + 
					string("dBm");
				netdetails->AddRow(k++, td);
			}
		}

		td[0] = " Encryption:";
		td[1] = "";
		if (ssid->cryptset == 0)
			td[1] = "None (Open)";
		if (ssid->cryptset == crypt_wep)
			td[1] = "WEP (Privacy bit set)";
		if (ssid->cryptset & crypt_layer3)
			td[1] += " Layer3";
		if (ssid->cryptset & crypt_wep40)
			td[1] += " WEP40";
		if (ssid->cryptset & crypt_wep104)
			td[1] += " WEP104";
		if (ssid->cryptset & crypt_wpa)
			td[1] += " WPA";
		if (ssid->cryptset & crypt_tkip)
			td[1] += " TKIP";
		if (ssid->cryptset & crypt_psk)
			td[1] += " PSK";
		if (ssid->cryptset & crypt_aes_ocb)
			td[1] += " AES-OCB";
		if (ssid->cryptset & crypt_aes_ccm)
			td[1] += " AES-CCM";
		if (ssid->cryptset & crypt_leap)
			td[1] += " LEAP";
		if (ssid->cryptset & crypt_ttls)
			td[1] += " TTLS";
		if (ssid->cryptset & crypt_tls)
			td[1] += " TLS";
		if (ssid->cryptset & crypt_peap)
			td[1] += " PEAP";
		if (ssid->cryptset & crypt_isakmp)
			td[1] += " ISA-KMP";
		if (ssid->cryptset & crypt_pptp)
			td[1] += " PPTP";
		if (ssid->cryptset & crypt_fortress)
			td[1] += " Fortress";
		if (ssid->cryptset & crypt_keyguard)
			td[1] += " Keyguard";
		netdetails->AddRow(k++, td);

		if (net->type == network_ap) {
			td[0] = " Beacon %:";
			if (ssid->beacons > ssid->beaconrate)
				ssid->beacons = ssid->beaconrate;
			osstr.str("");
			int brate = (int) (((double) ssid->beacons /
								(double) ssid->beaconrate) * 100);

			if (brate > 0) {

				osstr << setw(5) << left << fixed << setprecision(3) << brate;
				td[1] = osstr.str();
				netdetails->AddRow(k++, td);
			}
		}
	}

	return k;
}

int Kis_NetDetails_Panel::AppendNetworkInfo(int k, Kis_Display_NetGroup *tng,
											Netracker::tracked_network *net) {
	vector<Netracker::tracked_network *> *netvec = NULL;
	vector<string> td;
	ostringstream osstr;

	if (tng != NULL)
		netvec = tng->FetchNetworkVec();

	td.push_back("");
	td.push_back("");

	td[0] = "Name:";
	td[1] = tng->GetName(net);
	netdetails->AddRow(k++, td);

	if (net == NULL && netvec != NULL) {
		td[0] = "# Networks:";
		osstr.str("");
		osstr << netvec->size();
		td[1] = osstr.str();
		netdetails->AddRow(k++, td);
	}

	// Use the display metanet if we haven't been given one
	if (net == NULL && dng != NULL)
		net = dng->FetchNetwork();

	// Catch nulls just incase
	if (net == NULL)
		return k;

	td[0] = "BSSID:";
	td[1] = net->bssid.Mac2String();
	netdetails->AddRow(k++, td);

	td[0] = "Manuf:";
	td[1] = net->manuf;
	netdetails->AddRow(k++, td);

	td[0] = "First Seen:";
	osstr.str("");
	osstr << setw(14) << left << 
		(string(ctime((const time_t *) &(net->first_time)) + 4).substr(0, 15));
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	td[0] = "Last Seen:";
	osstr.str("");
	osstr << setw(14) << left << 
		(string(ctime((const time_t *) &(net->last_time)) + 4).substr(0, 15));
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	td[0] = "Type:";
	if (net->type == network_ap)
		td[1] = "Access Point (Managed/Infrastructure)";
	else if (net->type == network_probe)
		td[1] = "Probe (Client)";
	else if (net->type == network_turbocell)
		td[1] = "Turbocell";
	else if (net->type == network_data)
		td[1] = "Data Only (No management)";
	else if (net->type == network_mixed)
		td[1] = "Mixed (Multiple network types in group)";
	else
		td[1] = "Unknown";
	netdetails->AddRow(k++, td);

	td[0] = "Channel:";
	osstr.str("");
	if (net->channel != 0)
		osstr << net->channel;
	else
		osstr << "No channel identifying information seen";
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	for (map<unsigned int, unsigned int>::const_iterator fmi = 
		 net->freq_mhz_map.begin(); fmi != net->freq_mhz_map.end(); ++fmi) {
		float perc = ((float) fmi->second / 
					  (float) (net->llc_packets + net->data_packets)) * 100;

		int ch = FreqToChan(fmi->first);
		ostringstream chtxt;
		if (ch != 0)
			chtxt << ch;
		else
			chtxt << "Unk";


		td[0] = "Frequency:";
		osstr.str("");
		osstr << fmi->first << " (" << chtxt.str() << ") - " << 
			fmi->second << " packets, " <<
			setprecision(2) << perc << "%";
		td[1] = osstr.str();
		netdetails->AddRow(k++, td);
	}

	if (net->ssid_map.size() > 1) {
		if (net->lastssid != NULL) {
			td[0] = "Latest SSID:";
			td[1] = net->lastssid->ssid;
			netdetails->AddRow(k++, td);
		} else {
			td[1] = "No SSID data available";
			netdetails->AddRow(k++, td);
		}
	}

	for (map<uint32_t, Netracker::adv_ssid_data *>::iterator s = net->ssid_map.begin();
		 s != net->ssid_map.end(); ++s) {
		k = AppendSSIDInfo(k, net, s->second);
	}

	if (net->snrdata.last_signal_dbm == -256 || net->snrdata.last_signal_dbm == 0) {
		if (net->snrdata.last_signal_rssi == 0) {
			td[0] = "Signal:";
			td[1] = "No signal data available";
			netdetails->AddRow(k++, td);
		} else {
			td[0] = "Sig RSSI:";
			osstr.str("");
			osstr << net->snrdata.last_signal_rssi << " (max " <<
				net->snrdata.max_signal_rssi << ")";
			td[1] = osstr.str();
			netdetails->AddRow(k++, td);

			td[0] = "Noise RSSI:";
			osstr.str("");
			osstr << net->snrdata.last_noise_rssi << " (max " <<
				net->snrdata.max_noise_rssi << ")";
			td[1] = osstr.str();
			netdetails->AddRow(k++, td);
		}
	} else {
		td[0] = "Sig dBm";
		osstr.str("");
		osstr << net->snrdata.last_signal_dbm << " (max " <<
			net->snrdata.max_signal_dbm << ")";
		td[1] = osstr.str();
		netdetails->AddRow(k++, td);

		td[0] = "Noise dBm";
		osstr.str("");
		osstr << net->snrdata.last_noise_dbm << " (max " <<
			net->snrdata.max_noise_dbm << ")";
		td[1] = osstr.str();
		netdetails->AddRow(k++, td);
	}

	td[0] = "Packets:";
	osstr.str("");
	osstr << net->llc_packets + net->data_packets;
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	td[0] = "Data Pkts:";
	osstr.str("");
	osstr << net->data_packets;
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	td[0] = "Mgmt Pkts:";
	osstr.str("");
	osstr << net->llc_packets;
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	td[0] = "Crypt Pkts:";
	osstr.str("");
	osstr << net->crypt_packets;
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	td[0] = "Fragments:";
	osstr.str("");
	osstr << net->fragments << "/sec";
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	td[0] = "Retries:";
	osstr.str("");
	osstr << net->retries << "/sec";
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	td[0] = "Bytes:";
	osstr.str("");
	if (net->datasize < 1024) 
		osstr << net->datasize << "B";
	else if (net->datasize < (1024 * 1024)) 
		osstr << (int) (net->datasize / 1024) << "K";
	else 
		osstr << (int) (net->datasize / 1024 / 1024) << "M";
	td[1] = osstr.str();
	netdetails->AddRow(k++, td);

	map<uuid, KisPanelInterface::knc_card *> *cardmap =
		kpinterface->FetchNetCardMap();
	map<uuid, KisPanelInterface::knc_card *>::iterator kci;

	for (map<uuid, Netracker::source_data *>::iterator sdi = net->source_map.begin();
		 sdi != net->source_map.end(); ++sdi) {
		if ((kci = cardmap->find(sdi->second->source_uuid)) == cardmap->end()) {
			td[0] = "Seen By:";
			td[1] = string("(Unknown Card) ") + sdi->second->source_uuid.UUID2String();
			netdetails->AddRow(k++, td);
		} else {
			td[0] = "Seen By:";
			td[1] = kci->second->name + " (" + kci->second->interface + ")" +
				sdi->second->source_uuid.UUID2String();
			netdetails->AddRow(k++, td);
		}
		td[0] = "";
		osstr.str("");
		osstr << setw(14) << left << 
		(string(ctime((const time_t *) &(sdi->second->last_seen)) + 4).substr(0, 15));
		td[1] = osstr.str();
		netdetails->AddRow(k++, td);
	}

	if (net->cdp_dev_id.length() > 0) {
		td[0] = "CDP Device:";
		td[1] = net->cdp_dev_id;
		netdetails->AddRow(k++, td);

		td[0] = "CDP Port:";
		td[1] = net->cdp_port_id;
		netdetails->AddRow(k++, td);
	}

	if (netvec == NULL)
		return k;

	if (netvec->size() == 1)
		return k;

	for (unsigned int x = 0; x < netvec->size(); x++) {
		td[0] = "";
		td[1] = "-------";
		netdetails->AddRow(k++, td);
		k = AppendNetworkInfo(k, NULL, (*netvec)[x]);
	}

	return k;
}

int Kis_NetDetails_Panel::GraphTimer() {
	Kis_Display_NetGroup *tng, *ldng;
	Netracker::tracked_network *meta, *tmeta;
	int update = 0;

	if (kpinterface == NULL)
		return 1;

	ldng = dng;

	tng = kpinterface->FetchMainPanel()->FetchSelectedNetgroup();
	if (tng != NULL) {
		if (ldng == NULL) {
			ldng = tng;
			update = 1;
		} else {
			meta = ldng->FetchNetwork();
			tmeta = tng->FetchNetwork();

			if (meta == NULL && tmeta != NULL) {
				ldng = tng;
				update = 1;
			} else if (tmeta != NULL && last_mac != tmeta->bssid) {
				ClearGraphVectors();
				return 1;
			} else if (meta != NULL && last_dirty < meta->last_time) {
				update = 1;
			}
		}
	} else if (ldng != NULL) {
		ClearGraphVectors();
	}

	if (update && ldng != NULL) {
		meta = ldng->FetchNetwork();

		UpdateGraphVectors(meta->snrdata.last_signal_dbm == -256 ? 
						   meta->snrdata.last_signal_rssi : 
						   meta->snrdata.last_signal_dbm, 
						   meta->llc_packets + meta->data_packets,
						   meta->retries);
	}

	return 1;
}

void Kis_NetDetails_Panel::DrawPanel() {
	Kis_Display_NetGroup *tng;
	Netracker::tracked_network *meta, *tmeta;
	int update = 0;
	vector<string> td;

	int k = 0;

	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	// Figure out if we've changed
	tng = kpinterface->FetchMainPanel()->FetchSelectedNetgroup();
	if (tng != NULL) {
		if (dng == NULL) {
			dng = tng;
			update = 1;
		} else {
			meta = dng->FetchNetwork();
			tmeta = tng->FetchNetwork();

			if (meta == NULL && tmeta != NULL) {
				// We didn't have a valid metagroup before - we get the new one
				dng = tng;
				update = 1;
			} else if (tmeta != NULL && last_mac != tmeta->bssid) {
				// We weren't the same network before - we get the new one, clear the
				// graph vectors
				dng = tng;
				ClearGraphVectors();
				update = 1;
			} else if (meta != NULL && last_dirty < meta->last_time) {
				// The network has changed time - just update
				update = 1;
			}
		}
	} else if (dng != NULL) {
		// We've lost a selected network entirely, drop to null and update, clear the
		// graph vectors
		dng = NULL;
		ClearGraphVectors();
		update = 1;
	}

	if (update) {
		netdetails->Clear();

		if (dng != NULL)
			meta = dng->FetchNetwork();
		else
			meta = NULL;

		k = 0;
		td.push_back("");
		td.push_back("");

		if (dng != NULL) {
			td[0] = "";
			td[1] = "Group";
			netdetails->AddRow(k++, td);

			k = AppendNetworkInfo(k, tng, NULL);
		} else {
			td[0] = "";
			td[1] = "No network selected / Empty group selected";
			netdetails->AddRow(0, td);
		}
	}

	DrawComponentVec();

	wmove(win, 0, 0);
}

void Kis_NetDetails_Panel::ButtonAction(Kis_Panel_Component *in_button) {
	if (in_button == closebutton) {
		globalreg->panel_interface->KillPanel(this);
	} else if (in_button == nextbutton) {
		kpinterface->FetchMainPanel()->FetchDisplayNetlist()->KeyPress(KEY_DOWN);
		dng = NULL;
	} else if (in_button == prevbutton) {
		kpinterface->FetchMainPanel()->FetchDisplayNetlist()->KeyPress(KEY_UP);
		dng = NULL;
	}
}

void Kis_NetDetails_Panel::MenuAction(int opt) {
	// Menu processed an event, do something with it
	if (opt == mi_close) {
		globalreg->panel_interface->KillPanel(this);
		return;
	} else if (opt == mi_nextnet) {
		kpinterface->FetchMainPanel()->FetchDisplayNetlist()->KeyPress(KEY_DOWN);
		dng = NULL;
		return;
	} else if (opt == mi_prevnet) {
		kpinterface->FetchMainPanel()->FetchDisplayNetlist()->KeyPress(KEY_UP);
		dng = NULL;
		return;
	} else if (opt == mi_clients) {
		Kis_Clientlist_Panel *cl = new Kis_Clientlist_Panel(globalreg, kpinterface);
		kpinterface->AddPanel(cl);
	} else if (opt == mi_net || opt == mi_graphsig || opt == mi_graphpacket ||
			   opt == mi_graphretry) {
		UpdateViewMenu(opt);
	}
}

void Kis_NetDetails_Panel::UpdateViewMenu(int mi) {
	string opt;

	if (mi == mi_net) {
		opt = kpinterface->prefs->FetchOpt("DETAILS_SHOWNET");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("DETAILS_SHOWNET", "false", 1);
			menu->SetMenuItemChecked(mi_net, 0);
			netdetails->Hide();
		} else {
			kpinterface->prefs->SetOpt("DETAILS_SHOWNET", "true", 1);
			menu->SetMenuItemChecked(mi_net, 1);
			netdetails->Show();
		}
	} else if (mi == mi_graphsig) {
		opt = kpinterface->prefs->FetchOpt("DETAILS_SHOWGRAPHSIG");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("DETAILS_SHOWGRAPHSIG", "false", 1);
			menu->SetMenuItemChecked(mi_graphsig, 0);
			siggraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("DETAILS_SHOWGRAPHSIG", "true", 1);
			menu->SetMenuItemChecked(mi_graphsig, 1);
			siggraph->Show();
		}
	} else if (mi == mi_graphpacket) {
		opt = kpinterface->prefs->FetchOpt("DETAILS_SHOWGRAPHPACKET");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("DETAILS_SHOWGRAPHPACKET", "false", 1);
			menu->SetMenuItemChecked(mi_graphpacket, 0);
			packetgraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("DETAILS_SHOWGRAPHPACKET", "true", 1);
			menu->SetMenuItemChecked(mi_graphpacket, 1);
			packetgraph->Show();
		}
	} else if (mi == mi_graphretry) {
		opt = kpinterface->prefs->FetchOpt("DETAILS_SHOWGRAPHRETRY");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("DETAILS_SHOWGRAPHRETRY", "false", 1);
			menu->SetMenuItemChecked(mi_graphretry, 0);
			retrygraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("DETAILS_SHOWGRAPHRETRY", "true", 1);
			menu->SetMenuItemChecked(mi_graphretry, 1);
			retrygraph->Show();
		}
	} else if (mi == -1) {
		opt = kpinterface->prefs->FetchOpt("DETAILS_SHOWNET");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_net, 1);
			netdetails->Show();
		} else {
			menu->SetMenuItemChecked(mi_net, 0);
			netdetails->Hide();
		}

		opt = kpinterface->prefs->FetchOpt("DETAILS_SHOWGRAPHSIG");
		if (opt == "true") {
			menu->SetMenuItemChecked(mi_graphsig, 1);
			siggraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_graphsig, 0);
			siggraph->Hide();
		}

		opt = kpinterface->prefs->FetchOpt("DETAILS_SHOWGRAPHPACKET");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_graphpacket, 1);
			packetgraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_graphpacket, 0);
			packetgraph->Hide();
		}

		opt = kpinterface->prefs->FetchOpt("DETAILS_SHOWGRAPHRETRY");
		if (opt == "true") {
			menu->SetMenuItemChecked(mi_graphretry, 1);
			retrygraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_graphretry, 0);
			retrygraph->Hide();
		}
	}
}

int ChanDetailsButtonCB(COMPONENT_CALLBACK_PARMS) {
	((Kis_ChanDetails_Panel *) aux)->ButtonAction(component);
	return 1;
}

int ChanDetailsMenuCB(COMPONENT_CALLBACK_PARMS) {
	((Kis_ChanDetails_Panel *) aux)->MenuAction(status);
	return 1;
}

int ChanDetailsGraphEvent(TIMEEVENT_PARMS) {
	((Kis_ChanDetails_Panel *) parm)->GraphTimer();

	return 1;
}

void ChanDetailsCliConfigured(CLICONF_CB_PARMS) {
	((Kis_ChanDetails_Panel *) auxptr)->NetClientConfigured(kcli, recon);
}

void ChanDetailsCliAdd(KPI_ADDCLI_CB_PARMS) {
	((Kis_ChanDetails_Panel *) auxptr)->NetClientAdd(netcli, add);
}

void ChanDetailsProtoCHANNEL(CLIPROTO_CB_PARMS) {
	((Kis_ChanDetails_Panel *) auxptr)->Proto_CHANNEL(globalreg, proto_string,
													  proto_parsed, srccli, auxptr);
}

Kis_ChanDetails_Panel::Kis_ChanDetails_Panel(GlobalRegistry *in_globalreg,
											 KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	grapheventid =
		globalreg->timetracker->RegisterTimer(SERVER_TIMESLICES_SEC, NULL, 1,
											  &ChanDetailsGraphEvent, (void *) this);

	menu = new Kis_Menu(globalreg, this);

	menu->SetCallback(COMPONENT_CBTYPE_ACTIVATED, ChanDetailsMenuCB, this);

	mn_channels = menu->AddMenu("Channels", 0);
	mi_close = menu->AddMenuItem("Close window", mn_channels, 'w');

	mn_view = menu->AddMenu("View", 0);
	mi_chansummary = menu->AddMenuItem("Channel Summary", mn_view, 'c');
	menu->AddMenuItem("-", mn_view, 0);
	mi_signal = menu->AddMenuItem("Signal Level", mn_view, 's');
	mi_packets = menu->AddMenuItem("Packet Rate", mn_view, 'p');
	mi_traffic = menu->AddMenuItem("Data", mn_view, 'd');
	mi_networks = menu->AddMenuItem("Networks", mn_view, 'n');

	menu->Show();

	AddComponentVec(menu, KIS_PANEL_COMP_EVT);

	// Channel summary list gets titles but doesn't get the current one highlighted
	// and locks to fit inside the window
	chansummary = new Kis_Scrollable_Table(globalreg, this);
	chansummary->SetHighlightSelected(0);
	chansummary->SetLockScrollTop(1);
	chansummary->SetDrawTitles(1);
	AddComponentVec(chansummary, (KIS_PANEL_COMP_DRAW | KIS_PANEL_COMP_EVT |
								  KIS_PANEL_COMP_TAB));

	// Populate the titles
	vector<Kis_Scrollable_Table::title_data> titles;
	Kis_Scrollable_Table::title_data t;

	t.width = 4;
	t.title = "Chan";
	t.alignment = 0;
	titles.push_back(t);

	t.width = 7;
	t.title = "Packets";
	t.alignment = 2;
	titles.push_back(t);

	t.width = 3;
	t.title = "P/S";
	t.alignment = 2;
	titles.push_back(t);

	t.width = 5;
	t.title = "Data";
	t.alignment = 2;
	titles.push_back(t);

	t.width = 4;
	t.title = "Dt/s";
	t.alignment = 2;
	titles.push_back(t);

	t.width = 4;
	t.title = "Netw";
	t.alignment = 2;
	titles.push_back(t);

	t.width = 4;
	t.title = "ActN";
	t.alignment = 2;
	titles.push_back(t);

	t.width = 6;
	t.title = "Time";
	t.alignment = 2;
	titles.push_back(t);

	chansummary->AddTitles(titles);

	chansummary->Show();

	siggraph = new Kis_IntGraph(globalreg, this);
	siggraph->SetName("CHANNEL_SIG");
	siggraph->SetPreferredSize(0, 12);
	siggraph->SetScale(-110, -20);
	siggraph->SetInterpolation(0);
	siggraph->SetMode(0);
	siggraph->Show();
	siggraph->AddExtDataVec("Signal", 3, "channel_sig", "yellow,yellow",
							' ', ' ', 1, &sigvec);
	siggraph->AddExtDataVec("Noise", 4, "channel_noise", "green,green",
							' ', ' ', 1, &noisevec);
	// AddComponentVec(siggraph, KIS_PANEL_COMP_DRAW);

	packetgraph = new Kis_IntGraph(globalreg, this);
	packetgraph->SetName("CHANNEL_PPS");
	packetgraph->SetPreferredSize(0, 12);
	packetgraph->SetInterpolation(0);
	packetgraph->SetMode(0);
	packetgraph->Show();
	packetgraph->AddExtDataVec("Packet Rate", 4, "channel_pps", "green,green",
							   ' ', ' ', 1, &packvec);
	// AddComponentVec(packetgraph, KIS_PANEL_COMP_DRAW);

	bytegraph = new Kis_IntGraph(globalreg, this);
	bytegraph->SetName("CHANNEL_BPS");
	bytegraph->SetPreferredSize(0, 12);
	bytegraph->SetInterpolation(0);
	bytegraph->SetMode(0);
	bytegraph->Show();
	bytegraph->AddExtDataVec("Traffic", 4, "channel_bytes", "green,green",
							 ' ', ' ', 1, &bytevec);
	// AddComponentVec(bytegraph, KIS_PANEL_COMP_DRAW);

	netgraph = new Kis_IntGraph(globalreg, this);
	netgraph->SetName("CHANNEL_NETS");
	netgraph->SetPreferredSize(0, 12);
	netgraph->SetInterpolation(0);
	netgraph->SetMode(0);
	netgraph->Show();
	netgraph->AddExtDataVec("Networks", 3, "channel_nets", "yellow,yellow",
							' ', ' ', 1, &netvec);
	netgraph->AddExtDataVec("Active", 4, "channel_actnets", "green,green",
							' ', ' ', 1, &anetvec);
	// AddComponentVec(netgraph, KIS_PANEL_COMP_DRAW);

	SetTitle("");

	vbox = new Kis_Panel_Packbox(globalreg, this);
	vbox->SetPackV();
	vbox->SetHomogenous(0);
	vbox->SetSpacing(0);
	vbox->Show();

	vbox->Pack_End(siggraph, 0, 0);
	vbox->Pack_End(packetgraph, 0, 0);
	vbox->Pack_End(bytegraph, 0, 0);
	vbox->Pack_End(netgraph, 0, 0);
	vbox->Pack_End(chansummary, 0, 0);
	AddComponentVec(vbox, KIS_PANEL_COMP_DRAW);

	SetActiveComponent(chansummary);

	UpdateViewMenu(-1);
	GraphTimer();

	addref = kpinterface->Add_NetCli_AddCli_CB(ChanDetailsCliAdd, (void *) this);	

	main_component = vbox;

	Position(WIN_CENTER(LINES, COLS));
}

Kis_ChanDetails_Panel::~Kis_ChanDetails_Panel() {
	kpinterface->Remove_Netcli_AddCli_CB(addref);
	kpinterface->Remove_All_Netcli_Conf_CB(ChanDetailsCliConfigured);
	kpinterface->Remove_AllNetcli_ProtoHandler("CHANNEL", ChanDetailsProtoCHANNEL, this);
	globalreg->timetracker->RemoveTimer(grapheventid);
}

void Kis_ChanDetails_Panel::NetClientConfigured(KisNetClient *in_cli, int in_recon) {
	if (in_recon)
		return;

	if (in_cli->RegisterProtoHandler("CHANNEL", KCLI_CHANDETAILS_CHANNEL_FIELDS,
									 ChanDetailsProtoCHANNEL, this) < 0) {
		_MSG("Could not register CHANNEL protocol with remote server, connection "
			 "will be terminated", MSGFLAG_ERROR);
		in_cli->KillConnection();
	}
}

void Kis_ChanDetails_Panel::NetClientAdd(KisNetClient *in_cli, int add) {
	if (add == 0)
		return;

	in_cli->AddConfCallback(ChanDetailsCliConfigured, 1, this);
}

int Kis_ChanDetails_Panel::GraphTimer() {
	// Translates the channel map we get from the server into int vectors for 
	// the graphs, also populates the channel labels with the channel #s at
	// the appropriate positions.
	//
	// Also rewrites the channel summary table w/ the new data
	//
	// All in all this is a really expensive timer, but we only do it inside
	// the channel display window and its in the UI, so screw it

	// Update the vectors
	sigvec.clear();
	noisevec.clear();
	packvec.clear();
	bytevec.clear();
	netvec.clear();
	anetvec.clear();
	graph_label_vec.clear();
	chansummary->Clear();

	unsigned int chpos = 0;
	unsigned int tpos = 0;

	for (map<uint32_t, chan_sig_info *>::iterator x = channel_map.begin();
		 x != channel_map.end(); ++x) {
		if (x->second->sig_rssi != 0) {
			sigvec.push_back(x->second->sig_rssi);
			noisevec.push_back(x->second->noise_rssi);
		} else if (x->second->sig_dbm != 0) {
			sigvec.push_back(x->second->sig_dbm);
			if (x->second->noise_dbm == 0)
				noisevec.push_back(-256);
			else
				noisevec.push_back(x->second->noise_dbm);
		} else {
			sigvec.push_back(-256);
			noisevec.push_back(-256);
		}

		packvec.push_back(x->second->packets_delta);
		bytevec.push_back(x->second->bytes_delta);
		netvec.push_back(x->second->networks);
		anetvec.push_back(x->second->networks_active);

		Kis_IntGraph::graph_label lab;
		lab.position = chpos++;
		lab.label = IntToString(x->first);
		graph_label_vec.push_back(lab);

		// Populate the channel info table
		vector<string> td;
		td.push_back(IntToString(x->first));
		td.push_back(IntToString(x->second->packets));
		td.push_back(IntToString(x->second->packets_delta));

		if (x->second->bytes_seen < 1024) {
			td.push_back(IntToString(x->second->bytes_seen) + "B");
		} else if (x->second->bytes_seen < (1024 * 1024)) {
			td.push_back(IntToString(x->second->bytes_seen / 1024) + "K");
		} else {
			td.push_back(IntToString(x->second->bytes_seen / 1024 / 1024) + "M");
		}
		if (x->second->bytes_delta < 1024) {
			td.push_back(IntToString(x->second->bytes_delta) + "B");
		} else if (x->second->bytes_delta < (1024 * 1024)) {
			td.push_back(IntToString(x->second->bytes_delta / 1024) + "K");
		} else {
			td.push_back(IntToString(x->second->bytes_delta / 1024 / 1024) + "M");
		}

		td.push_back(IntToString(x->second->networks));
		td.push_back(IntToString(x->second->networks_active));

		td.push_back(NtoString<float>((float) x->second->channel_time_on / 
									  1000000).Str() + "s");

		chansummary->AddRow(tpos++, td);
	}

	siggraph->SetXLabels(graph_label_vec, "Signal");
	packetgraph->SetXLabels(graph_label_vec, "Packet Rate");
	bytegraph->SetXLabels(graph_label_vec, "Traffic");
	netgraph->SetXLabels(graph_label_vec, "Networks");

	return 1;
}

void Kis_ChanDetails_Panel::DrawPanel() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	DrawComponentVec();

	wmove(win, 0, 0);
}

void Kis_ChanDetails_Panel::ButtonAction(Kis_Panel_Component *in_button) {
	return;
}

void Kis_ChanDetails_Panel::MenuAction(int opt) {
	if (opt == mi_close) {
		globalreg->panel_interface->KillPanel(this);
		return;
	} else {
		UpdateViewMenu(opt);
	}
}

void Kis_ChanDetails_Panel::UpdateViewMenu(int mi) {
	string opt;

	if (mi == mi_chansummary) {
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWSUM");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWSUM", "false", 1);
			menu->SetMenuItemChecked(mi_chansummary, 0);
			chansummary->Hide();
		} else {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWSUM", "true", 1);
			menu->SetMenuItemChecked(mi_chansummary, 1);
			chansummary->Show();
		}
	} else if (mi == mi_signal) {
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWSIG");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWSIG", "false", 1);
			menu->SetMenuItemChecked(mi_signal, 0);
			siggraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWSIG", "true", 1);
			menu->SetMenuItemChecked(mi_signal, 1);
			siggraph->Show();
		}
	} else if (mi == mi_packets) {
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWPACK");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWPACK", "false", 1);
			menu->SetMenuItemChecked(mi_packets, 0);
			packetgraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWPACK", "true", 1);
			menu->SetMenuItemChecked(mi_packets, 1);
			packetgraph->Show();
		}
	} else if (mi == mi_traffic) {
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWTRAF");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWTRAF", "false", 1);
			menu->SetMenuItemChecked(mi_traffic, 0);
			bytegraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWTRAF", "true", 1);
			menu->SetMenuItemChecked(mi_traffic, 1);
			bytegraph->Show();
		}
	} else if (mi == mi_networks) {
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWNET");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWNET", "false", 1);
			menu->SetMenuItemChecked(mi_networks, 0);
			netgraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("CHANDETAILS_SHOWNET", "true", 1);
			menu->SetMenuItemChecked(mi_networks, 1);
			netgraph->Show();
		}
	} else if (mi == -1) {
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWSUM");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_chansummary, 1);
			chansummary->Show();
		} else {
			menu->SetMenuItemChecked(mi_chansummary, 0);
			chansummary->Hide();
		}
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWSIG");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_signal, 1);
			siggraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_signal, 0);
			siggraph->Hide();
		}
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWPACK");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_packets, 1);
			packetgraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_packets, 0);
			packetgraph->Hide();
		}
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWTRAF");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_traffic, 1);
			bytegraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_traffic, 0);
			bytegraph->Hide();
		}
		opt = kpinterface->prefs->FetchOpt("CHANDETAILS_SHOWNET");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_networks, 1);
			netgraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_networks, 0);
			netgraph->Hide();
		}
	}
}

void Kis_ChanDetails_Panel::Proto_CHANNEL(CLIPROTO_CB_PARMS) {
	if (proto_parsed->size() < KCLI_CHANDETAILS_CHANNEL_NUMFIELDS)
		return;

	int fnum = 0;

	chan_sig_info *ci;

	int tint;
	long int tlong;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1) {
		return;
	}

	if (channel_map.find(tint) != channel_map.end()) {
		ci = channel_map[tint];
	} else {
		ci = new chan_sig_info;
		ci->channel = tint;
		channel_map[tint] = ci;
	}

	ci->last_updated = time(0);

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1) 
		return;
	if (tint != 0)
		ci->channel_time_on = tint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1)
		return;
	ci->packets = tint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1)
		return;
	if (tint != 0)
		ci->packets_delta = tint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%ld", &tlong) != 1)
		return;
	ci->usec_used = tlong;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%ld", &tlong) != 1)
		return;
	ci->bytes_seen = tlong;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%ld", &tlong) != 1)
		return;
	if (tlong != 0)
		ci->bytes_delta = tlong;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1)
		return;
	ci->networks = tint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1)
		return;
	ci->networks_active = tint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1)
		return;
	ci->sig_dbm = tint;
	
	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1)
		return;
	ci->sig_rssi = tint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1)
		return;
	ci->noise_dbm = tint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%d", &tint) != 1)
		return;
	ci->noise_rssi = tint;
}

int CliDetailsButtonCB(COMPONENT_CALLBACK_PARMS) {
	((Kis_ClientDetails_Panel *) aux)->ButtonAction(component);
	return 1;
}

int CliDetailsMenuCB(COMPONENT_CALLBACK_PARMS) {
	((Kis_ClientDetails_Panel *) aux)->MenuAction(status);
	return 1;
}

int CliDetailsGraphEvent(TIMEEVENT_PARMS) {
	return ((Kis_ClientDetails_Panel *) parm)->GraphTimer();
}

Kis_ClientDetails_Panel::Kis_ClientDetails_Panel(GlobalRegistry *in_globalreg, 
												 KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	grapheventid =
		globalreg->timetracker->RegisterTimer(SERVER_TIMESLICES_SEC, NULL, 1,
											  &CliDetailsGraphEvent, (void *) this);

	menu = new Kis_Menu(globalreg, this);

	menu->SetCallback(COMPONENT_CBTYPE_ACTIVATED, CliDetailsMenuCB, this);

	mn_client = menu->AddMenu("Client", 0);
	mi_nextcli = menu->AddMenuItem("Next client", mn_client, 'n');
	mi_prevcli = menu->AddMenuItem("Prev client", mn_client, 'p');
	menu->AddMenuItem("-", mn_client, 0);

	mn_preferences = menu->AddSubMenuItem("Preferences", mn_client, 'P');
	mi_clicolprefs = menu->AddMenuItem("Client Columns...", mn_preferences, 'N');
	mi_cliextraprefs = menu->AddMenuItem("Client Extras...", mn_preferences, 'E');

	menu->AddMenuItem("-", mn_client, 0);
	mi_close = menu->AddMenuItem("Close window", mn_client, 'w');

	mn_view = menu->AddMenu("View", 0);
	mi_cli = menu->AddMenuItem("Client Details", mn_view, 'c');
	menu->AddMenuItem("-", mn_view, 0);
	mi_graphsig = menu->AddMenuItem("Signal Level", mn_view, 's');
	mi_graphpacket = menu->AddMenuItem("Packet Rate", mn_view, 'p');
	mi_graphretry = menu->AddMenuItem("Retry Rate", mn_view, 'r');

	menu->Show();
	AddComponentVec(menu, KIS_PANEL_COMP_EVT);

	clientdetails = new Kis_Scrollable_Table(globalreg, this);
	clientdetails->SetHighlightSelected(0);
	clientdetails->SetLockScrollTop(1);
	clientdetails->SetDrawTitles(0);
	AddComponentVec(clientdetails, (KIS_PANEL_COMP_DRAW | KIS_PANEL_COMP_EVT |
									KIS_PANEL_COMP_TAB));

	vector<Kis_Scrollable_Table::title_data> titles;
	Kis_Scrollable_Table::title_data t;
	t.width = 12;
	t.title = "field";
	t.alignment = 2;
	titles.push_back(t);
	t.width = 0;
	t.title = "value";
	t.alignment = 0;
	titles.push_back(t);

	clientdetails->AddTitles(titles);

	clientdetails->Show();

	siggraph = new Kis_IntGraph(globalreg, this);
	siggraph->SetName("DETAIL_SIG");
	siggraph->SetPreferredSize(0, 8);
	siggraph->SetScale(-110, -40);
	siggraph->SetInterpolation(1);
	siggraph->SetMode(0);
	siggraph->Show();
	siggraph->AddExtDataVec("Signal", 4, "graph_detail_sig", "yellow,yellow", 
		 					  ' ', ' ', 1, &sigpoints);
	AddComponentVec(siggraph, KIS_PANEL_COMP_EVT);

	packetgraph = new Kis_IntGraph(globalreg, this);
	packetgraph->SetName("DETAIL_PPS");
	packetgraph->SetPreferredSize(0, 8);
	packetgraph->SetScale(0, 0);
	packetgraph->SetInterpolation(1);
	packetgraph->SetMode(0);
	packetgraph->Show();
	packetgraph->AddExtDataVec("Packet Rate", 4, "graph_detail_pps", "green,green", 
							  ' ', ' ', 1, &packetpps);
	AddComponentVec(packetgraph, KIS_PANEL_COMP_EVT);

	retrygraph = new Kis_IntGraph(globalreg, this);
	retrygraph->SetName("DETAIL_RETRY_PPS");
	retrygraph->SetPreferredSize(0, 8);
	retrygraph->SetScale(0, 0);
	retrygraph->SetInterpolation(1);
	retrygraph->SetMode(0);
	retrygraph->Show();
	retrygraph->AddExtDataVec("Retry Rate", 4, "graph_detail_retrypps", "red,red", 
							  ' ', ' ', 1, &retrypps);
	AddComponentVec(retrygraph, KIS_PANEL_COMP_EVT);

	ClearGraphVectors();

	SetTitle("");

	vbox = new Kis_Panel_Packbox(globalreg, this);
	vbox->SetPackV();
	vbox->SetHomogenous(0);
	vbox->SetSpacing(0);
	vbox->Show();

	vbox->Pack_End(siggraph, 0, 0);
	vbox->Pack_End(packetgraph, 0, 0);
	vbox->Pack_End(retrygraph, 0, 0);

	vbox->Pack_End(clientdetails, 1, 0);

	AddComponentVec(vbox, KIS_PANEL_COMP_DRAW);

	last_dirty = 0;
	last_mac = mac_addr(0);
	dng = NULL;
	dcli = NULL;

	vector<string> td;
	td.push_back("");
	td.push_back("No client selected");
	clientdetails->AddRow(0, td);

	main_component = vbox;

	SetActiveComponent(clientdetails);

	clientlist = NULL;

	UpdateViewMenu(-1);

	Position(WIN_CENTER(LINES, COLS));
}

Kis_ClientDetails_Panel::~Kis_ClientDetails_Panel() {
	if (grapheventid >= 0 && globalreg != NULL)
		globalreg->timetracker->RemoveTimer(grapheventid);
}

void Kis_ClientDetails_Panel::ClearGraphVectors() {
	lastpackets = 0;
	sigpoints.clear();
	packetpps.clear();
	retrypps.clear();
	for (unsigned int x = 0; x < 120; x++) {
		sigpoints.push_back(-256);
		packetpps.push_back(0);
		retrypps.push_back(0);
	}
}

void Kis_ClientDetails_Panel::UpdateGraphVectors(int signal, int pps, int retry) {
	sigpoints.push_back(signal);
	if (sigpoints.size() > 120)
		sigpoints.erase(sigpoints.begin(), sigpoints.begin() + sigpoints.size() - 120);

	if (lastpackets == 0)
		lastpackets = pps;
	packetpps.push_back(pps - lastpackets);
	lastpackets = pps;
	if (packetpps.size() > 120)
		packetpps.erase(packetpps.begin(), packetpps.begin() + packetpps.size() - 120);

	retrypps.push_back(retry);
	if (retrypps.size() > 120)
		retrypps.erase(retrypps.begin(), retrypps.begin() + retrypps.size() - 120);
}

int Kis_ClientDetails_Panel::GraphTimer() {
	Netracker::tracked_client *ldcli;

	if (clientlist == NULL)
		return 1;

	if (kpinterface == NULL)
		return 1;

	ldcli = clientlist->FetchSelectedClient();
	if (ldcli != NULL) {
		if (ldcli != dcli) 
			ClearGraphVectors();
	} else {
		ClearGraphVectors();
		return 1;
	}

	UpdateGraphVectors(ldcli->snrdata.last_signal_dbm == -256 ?
					   ldcli->snrdata.last_signal_rssi :
					   ldcli->snrdata.last_signal_dbm,
					   ldcli->llc_packets + ldcli->data_packets,
					   ldcli->retries);

	return 1;
}

void Kis_ClientDetails_Panel::DrawPanel() {
	Netracker::tracked_client *tcli = NULL;
	int update = 0;
	vector<string> td;
	ostringstream osstr;

	int k = 0;

	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	if (clientlist != NULL) {
		tcli = clientlist->FetchSelectedClient();
		if (tcli == NULL) {
			dcli = tcli;
			update = 1;
			ClearGraphVectors();
		} else if (tcli != dcli) {
			dcli = tcli;
			update = 1;
			ClearGraphVectors();
		} else { 
			if (dcli->last_time != last_dirty)
				update = 1;
		}
	} else if (dcli != NULL) {
		dcli = NULL;
		update = 1;
		ClearGraphVectors();
	}

	td.push_back("");
	td.push_back("");

	if (update) {
		clientdetails->Clear();

		if (dcli == NULL) {
			td[0] = "";
			td[1] = "No client selected";
			clientdetails->AddRow(0, td);
		} else {
			td[0] = "MAC:";
			td[1] = dcli->mac.Mac2String();
			clientdetails->AddRow(k++, td);

			td[0] = "Manuf:";
			td[1] = dcli->manuf;
			clientdetails->AddRow(k++, td);

			td[0] = "Network:";
			td[1] = dcli->bssid.Mac2String();
			clientdetails->AddRow(k++, td);

			if (dcli->netptr != NULL) {
				td[0] = "Net Manuf:";
				td[1] = dcli->netptr->manuf;
				clientdetails->AddRow(k++, td);
			}

			td[0] = "Type:";
			if (dcli->type == client_unknown)
				td[1] = "Unknown";
			else if (dcli->type == client_fromds) 
				td[1] = "Wired (traffic from AP only)";
			else if (dcli->type == client_tods)
				td[1] = "Wireless (traffic from wireless only)";
			else if (dcli->type == client_interds)
				td[1] = "Inter-AP traffic (WDS)";
			else if (dcli->type == client_established)
				td[1] = "Wireless (traffic to and from AP)";
			else if (dcli->type == client_adhoc)
				td[1] = "Wireless Ad-Hoc";
			clientdetails->AddRow(k++, td);

			td[0] = "First Seen:";
			osstr.str("");
			osstr << setw(14) << left <<
				(string(ctime((const time_t *) &(dcli->first_time)) + 4).substr(0, 15));
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			td[0] = "Last Seen:";
			osstr.str("");
			osstr << setw(14) << left <<
				(string(ctime((const time_t *) &(dcli->last_time)) + 4).substr(0, 15));
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			td[0] = "Probed Networks:";
			for (map<uint32_t, Netracker::adv_ssid_data *>::iterator si =
				 dcli->ssid_map.begin(); si != dcli->ssid_map.end(); ++si) {
				osstr.str("");
				osstr << si->second->ssid << " ";

				if (si->second->cryptset == 0)
					osstr << "(No crypto)";
				else if (si->second->cryptset == crypt_wep)
					osstr << "(WEP)";
				else 
					osstr << "(";

				if (si->second->cryptset & crypt_layer3)
					osstr << " Layer3";
				if (si->second->cryptset & crypt_wep40)
					osstr << " WEP40";
				if (si->second->cryptset & crypt_wep104)
					osstr << " WEP104";
				if (si->second->cryptset & crypt_wpa)
					osstr << " WPA";
				if (si->second->cryptset & crypt_tkip)
					osstr << " TKIP";
				if (si->second->cryptset & crypt_psk)
					osstr << " PSK";
				if (si->second->cryptset & crypt_aes_ocb)
					osstr << " AES-OCB";
				if (si->second->cryptset & crypt_aes_ccm)
					osstr << " AES-CCM";
				if (si->second->cryptset & crypt_leap)
					osstr << " LEAP";
				if (si->second->cryptset & crypt_ttls)
					osstr << " TTLS";
				if (si->second->cryptset & crypt_tls)
					osstr << " TLS";
				if (si->second->cryptset & crypt_peap)
					osstr << " PEAP";
				if (si->second->cryptset & crypt_isakmp)
					osstr << " ISA-KMP";
				if (si->second->cryptset & crypt_pptp)
					osstr << " PPTP";
				if (si->second->cryptset & crypt_fortress)
					osstr << " Fortress";
				if (si->second->cryptset & crypt_keyguard)
					osstr << " Keyguard";

				if (si->second->cryptset != 0 &&
					si->second->cryptset != crypt_wep) 
					osstr << " )";

				td[1] = osstr.str();

				clientdetails->AddRow(k++, td);
				td[0] = "";
			}

			td[0] = "Decrypted:";
			td[1] = dcli->decrypted ? "Yes" : "No";
			clientdetails->AddRow(k++, td);

			td[0] = "Frequency:";
			for (map<unsigned int, unsigned int>::const_iterator fmi = 
				 dcli->freq_mhz_map.begin(); fmi != dcli->freq_mhz_map.end(); ++fmi) {
				float perc = ((float) fmi->second / 
							  (float) (dcli->llc_packets + dcli->data_packets)) * 100;

				int ch = FreqToChan(fmi->first);
				ostringstream chtxt;
				if (ch != 0)
					chtxt << ch;
				else
					chtxt << "Unk";

				osstr.str("");
				osstr << fmi->first << " (" << chtxt.str() << ") - " << 
					fmi->second << " packets, " <<
					setprecision(2) << perc << "%";
				td[1] = osstr.str();
				clientdetails->AddRow(k++, td);
				td[0] = "";
			}

			if (dcli->snrdata.last_signal_dbm == -256 || 
				dcli->snrdata.last_signal_dbm == 0) {
				if (dcli->snrdata.last_signal_rssi == 0) {
					td[0] = "Signal:";
					td[1] = "No signal data available";
					clientdetails->AddRow(k++, td);
				} else {
					td[0] = "Sig RSSI:";
					osstr.str("");
					osstr << dcli->snrdata.last_signal_rssi << " (max " <<
						dcli->snrdata.max_signal_rssi << ")";
					td[1] = osstr.str();
					clientdetails->AddRow(k++, td);

					td[0] = "Noise RSSI:";
					osstr.str("");
					osstr << dcli->snrdata.last_noise_rssi << " (max " <<
						dcli->snrdata.max_noise_rssi << ")";
					td[1] = osstr.str();
					clientdetails->AddRow(k++, td);
				}
			} else {
				td[0] = "Sig dBm";
				osstr.str("");
				osstr << dcli->snrdata.last_signal_dbm << " (max " <<
					dcli->snrdata.max_signal_dbm << ")";
				td[1] = osstr.str();
				clientdetails->AddRow(k++, td);

				td[0] = "Noise dBm";
				osstr.str("");
				osstr << dcli->snrdata.last_noise_dbm << " (max " <<
					dcli->snrdata.max_noise_dbm << ")";
				td[1] = osstr.str();
				clientdetails->AddRow(k++, td);
			}

			td[0] = "Packets:";
			osstr.str("");
			osstr << dcli->llc_packets + dcli->data_packets;
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			td[0] = "Data Pkts:";
			osstr.str("");
			osstr << dcli->data_packets;
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			td[0] = "Mgmt Pkts:";
			osstr.str("");
			osstr << dcli->llc_packets;
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			td[0] = "Crypt Pkts:";
			osstr.str("");
			osstr << dcli->crypt_packets;
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			td[0] = "Fragments:";
			osstr.str("");
			osstr << dcli->fragments << "/sec";
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			td[0] = "Retries:";
			osstr.str("");
			osstr << dcli->retries << "/sec";
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			td[0] = "Bytes:";
			osstr.str("");
			if (dcli->datasize < 1024) 
				osstr << dcli->datasize << "B";
			else if (dcli->datasize < (1024 * 1024)) 
				osstr << (int) (dcli->datasize / 1024) << "K";
			else 
				osstr << (int) (dcli->datasize / 1024 / 1024) << "M";
			td[1] = osstr.str();
			clientdetails->AddRow(k++, td);

			map<uuid, KisPanelInterface::knc_card *> *cardmap =
				kpinterface->FetchNetCardMap();
			map<uuid, KisPanelInterface::knc_card *>::iterator kci;

			for (map<uuid, Netracker::source_data *>::iterator sdi = 
				 dcli->source_map.begin();
				 sdi != dcli->source_map.end(); ++sdi) {
				if ((kci = cardmap->find(sdi->second->source_uuid)) == cardmap->end()) {
					td[0] = "Seen By:";
					td[1] = string("(Unknown Card) ") + 
						sdi->second->source_uuid.UUID2String();
					clientdetails->AddRow(k++, td);
				} else {
					td[0] = "Seen By:";
					td[1] = kci->second->name + " (" + kci->second->interface + ") " +
						sdi->second->source_uuid.UUID2String();
					clientdetails->AddRow(k++, td);
				}
				td[0] = "";
				osstr.str("");
				osstr << setw(14) << left << 
					(string(ctime((const time_t *) &(sdi->second->last_seen)) + 4).substr(0, 15));
				td[1] = osstr.str();
				clientdetails->AddRow(k++, td);
			}

			if (dcli->cdp_dev_id.length() > 0) {
				td[0] = "CDP Device:";
				td[1] = dcli->cdp_dev_id;
				clientdetails->AddRow(k++, td);

				td[0] = "CDP Port:";
				td[1] = dcli->cdp_port_id;
				clientdetails->AddRow(k++, td);
			}

			if (dcli->dhcp_host.length() > 0) {
				td[0] = "DHCP Name:";
				td[1] = dcli->dhcp_host;
				clientdetails->AddRow(k++, td);
			}

			if (dcli->dhcp_vendor.length() > 0) {
				td[0] = "DHCP OS:";
				td[1] = dcli->dhcp_vendor;
				clientdetails->AddRow(k++, td);
			}
		}
	}

	DrawComponentVec();
	wmove(win, 0, 0);
}

void Kis_ClientDetails_Panel::ButtonAction(Kis_Panel_Component *in_button) {
	return;
}

void Kis_ClientDetails_Panel::MenuAction(int opt) {
	if (opt == mi_close) {
		globalreg->panel_interface->KillPanel(this);
		return;
	} else if (opt == mi_clicolprefs) {
		Kis_ColumnPref_Panel *cpp = new Kis_ColumnPref_Panel(globalreg, kpinterface);

		for (unsigned int x = 0; client_column_details[x].pref != NULL; x++) {
			cpp->AddColumn(client_column_details[x].pref,
						   client_column_details[x].name);
		}

		cpp->ColumnPref("clientlist_columns", "Client List");
		kpinterface->AddPanel(cpp);
	} else if (opt == mi_cliextraprefs) {
		Kis_ColumnPref_Panel *cpp = new Kis_ColumnPref_Panel(globalreg, kpinterface);

		for (unsigned int x = 0; client_extras_details[x].pref != NULL; x++) {
			cpp->AddColumn(client_extras_details[x].pref,
						   client_extras_details[x].name);
		}

		cpp->ColumnPref("clientlist_extras", "Client Extras");
		kpinterface->AddPanel(cpp);
	} else if (opt == mi_nextcli && clientlist != NULL) {
		clientlist->KeyPress(KEY_DOWN);
		dcli = NULL;
		return;
	} else if (opt == mi_prevcli && clientlist != NULL) {
		clientlist->KeyPress(KEY_UP);
		dcli = NULL;
		return;
	} else if (opt == mi_cli || opt == mi_graphsig ||
			   opt == mi_graphpacket || opt == mi_graphretry) {
		UpdateViewMenu(opt);
		return;
	}
}

void Kis_ClientDetails_Panel::UpdateViewMenu(int mi) {
	string opt;

	if (mi == mi_cli) {
		opt = kpinterface->prefs->FetchOpt("CLIDETAILS_SHOWCLI");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CLIDETAILS_SHOWCLI", "false", 1);
			menu->SetMenuItemChecked(mi_cli, 0);
			clientdetails->Hide();
		} else {
			kpinterface->prefs->SetOpt("CLIDETAILS_SHOWCLI", "true", 1);
			menu->SetMenuItemChecked(mi_cli, 1);
			clientdetails->Show();
		}
	} else if (mi == mi_graphsig) {
		opt = kpinterface->prefs->FetchOpt("CLIDETAILS_SHOWGRAPHSIG");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CLIDETAILS_SHOWGRAPHSIG", "false", 1);
			menu->SetMenuItemChecked(mi_graphsig, 0);
			siggraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("CLIDETAILS_SHOWGRAPHSIG", "true", 1);
			menu->SetMenuItemChecked(mi_graphsig, 1);
			siggraph->Show();
		}
	} else if (mi == mi_graphpacket) {
		opt = kpinterface->prefs->FetchOpt("CLIDETAILS_SHOWGRAPHPACKET");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CLIDETAILS_SHOWGRAPHPACKET", "false", 1);
			menu->SetMenuItemChecked(mi_graphpacket, 0);
			packetgraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("CLIDETAILS_SHOWGRAPHPACKET", "true", 1);
			menu->SetMenuItemChecked(mi_graphpacket, 1);
			packetgraph->Show();
		}
	} else if (mi == mi_graphretry) {
		opt = kpinterface->prefs->FetchOpt("CLIDETAILS_SHOWGRAPHRETRY");
		if (opt == "" || opt == "true") {
			kpinterface->prefs->SetOpt("CLIDETAILS_SHOWGRAPHRETRY", "false", 1);
			menu->SetMenuItemChecked(mi_graphretry, 0);
			retrygraph->Hide();
		} else {
			kpinterface->prefs->SetOpt("CLIDETAILS_SHOWGRAPHRETRY", "true", 1);
			menu->SetMenuItemChecked(mi_graphretry, 1);
			retrygraph->Show();
		}
	} else if (mi == -1) {
		opt = kpinterface->prefs->FetchOpt("CLIDETAILS_SHOWCLI");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_cli, 1);
			clientdetails->Show();
		} else {
			menu->SetMenuItemChecked(mi_cli, 0);
			clientdetails->Hide();
		}

		opt = kpinterface->prefs->FetchOpt("CLIDETAILS_SHOWGRAPHSIG");
		if (opt == "true") {
			menu->SetMenuItemChecked(mi_graphsig, 1);
			siggraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_graphsig, 0);
			siggraph->Hide();
		}

		opt = kpinterface->prefs->FetchOpt("CLIDETAILS_SHOWGRAPHPACKET");
		if (opt == "" || opt == "true") {
			menu->SetMenuItemChecked(mi_graphpacket, 1);
			packetgraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_graphpacket, 0);
			packetgraph->Hide();
		}

		opt = kpinterface->prefs->FetchOpt("CLIDETAILS_SHOWGRAPHRETRY");
		if (opt == "true") {
			menu->SetMenuItemChecked(mi_graphretry, 1);
			retrygraph->Show();
		} else {
			menu->SetMenuItemChecked(mi_graphretry, 0);
			retrygraph->Hide();
		}
	}
}

int AlertDetailsButtonCB(COMPONENT_CALLBACK_PARMS) {
	((Kis_AlertDetails_Panel *) aux)->ButtonAction(component);
	return 1;
}

int AlertDetailsMenuCB(COMPONENT_CALLBACK_PARMS) {
	((Kis_AlertDetails_Panel *) aux)->MenuAction(status);
	return 1;
}

class KisAlert_Sort_Time {
public:
	inline bool operator()(KisPanelInterface::knc_alert *x, 
						   KisPanelInterface::knc_alert *y) const {
		if (x->tv.tv_sec < y->tv.tv_sec ||
			(x->tv.tv_sec == y->tv.tv_sec && x->tv.tv_usec < y->tv.tv_usec))
			return 1;

		return 0;
	}
};

class KisAlert_Sort_TimeInv {
public:
	inline bool operator()(KisPanelInterface::knc_alert *x, 
						   KisPanelInterface::knc_alert *y) const {
		if (x->tv.tv_sec < y->tv.tv_sec ||
			(x->tv.tv_sec == y->tv.tv_sec && x->tv.tv_usec < y->tv.tv_usec))
			return 0;

		return 1;
	}
};

class KisAlert_Sort_Type {
public:
	inline bool operator()(KisPanelInterface::knc_alert *x, 
						   KisPanelInterface::knc_alert *y) const {
		return x->alertname < y->alertname;
	}
};

class KisAlert_Sort_Bssid {
public:
	inline bool operator()(KisPanelInterface::knc_alert *x, 
						   KisPanelInterface::knc_alert *y) const {
		return x->bssid < y->bssid;
	}
};

Kis_AlertDetails_Panel::Kis_AlertDetails_Panel(GlobalRegistry *in_globalreg, 
											   KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	last_alert = NULL;

	menu = new Kis_Menu(globalreg, this);

	menu->SetCallback(COMPONENT_CBTYPE_ACTIVATED, CliDetailsMenuCB, this);

	mn_alert = menu->AddMenu("Alert", 0);
	mi_clear = menu->AddMenuItem("Clear alerts", mn_alert, 'c');
	menu->AddMenuItem("-", mn_alert, 0);
	mi_close = menu->AddMenuItem("Close window", mn_alert, 'w');

	mn_sort = menu->AddMenu("Sort", 0);
	mi_time = menu->AddMenuItem("Time", mn_sort, 't');
	mi_latest = menu->AddMenuItem("Latest", mn_sort, 'l');
	mi_type = menu->AddMenuItem("Type", mn_sort, 'T');
	mi_bssid = menu->AddMenuItem("BSSID", mn_sort, 'b');

	menu->Show();
	AddComponentVec(menu, KIS_PANEL_COMP_EVT);

	alertlist = new Kis_Scrollable_Table(globalreg, this);
	alertlist->SetHighlightSelected(1);
	alertlist->SetLockScrollTop(1);
	alertlist->SetDrawTitles(0);
	AddComponentVec(alertlist, (KIS_PANEL_COMP_DRAW | KIS_PANEL_COMP_EVT |
								KIS_PANEL_COMP_TAB));

	vector<Kis_Scrollable_Table::title_data> titles;
	Kis_Scrollable_Table::title_data t;
	t.width = 8;
	t.title = "time";
	t.alignment = 2;
	titles.push_back(t);
	t.width = 10;
	t.title = "header";
	t.alignment = 0;
	titles.push_back(t);
	t.width = 0;
	t.title = "text";
	t.alignment = 0;

	alertlist->AddTitles(titles);
	alertlist->Show();

	alertdetails = new Kis_Scrollable_Table(globalreg, this);
	alertdetails->SetHighlightSelected(0);
	alertdetails->SetLockScrollTop(1);
	alertdetails->SetDrawTitles(0);
	AddComponentVec(alertdetails, (KIS_PANEL_COMP_DRAW | KIS_PANEL_COMP_EVT |
								KIS_PANEL_COMP_TAB));

	titles.clear();

	t.width = 12;
	t.title = "field";
	t.alignment = 2;
	titles.push_back(t);
	t.width = 0;
	t.title = "text";
	t.alignment = 0;

	alertdetails->AddTitles(titles);
	alertdetails->SetPreferredSize(0, 5);
	alertdetails->Show();

	SetTitle("");

	vbox = new Kis_Panel_Packbox(globalreg, this);
	vbox->SetPackV();
	vbox->SetHomogenous(0);
	vbox->SetSpacing(0);
	vbox->Show();

	vbox->Pack_End(alertlist, 0, 0);
	vbox->Pack_End(alertdetails, 0, 0);

	AddComponentVec(vbox, KIS_PANEL_COMP_DRAW);

	vector<string> td;
	td.push_back("");
	td.push_back("");
	td.push_back("No alerts");
	alertlist->AddRow(0, td);

	main_component = vbox;

	SetActiveComponent(alertlist);

	UpdateSortPrefs();
	UpdateSortMenu(-1);

	Position(WIN_CENTER(LINES, COLS));
}

Kis_AlertDetails_Panel::~Kis_AlertDetails_Panel() {

}

void Kis_AlertDetails_Panel::DrawPanel() {
	vector<KisPanelInterface::knc_alert *> *raw_alerts = kpinterface->FetchAlertVec();
	int k = 0;
	vector<string> td;

	// No custom drawing if we have no alerts or no changes
	
	if (raw_alerts->size() == 0)
		return;

	if ((*raw_alerts)[raw_alerts->size() - 1] == last_alert && UpdateSortPrefs() == 0)
		return;

	sorted_alerts = *raw_alerts;

	switch (sort_mode) {
		case alertsort_time:
			stable_sort(sorted_alerts.begin(), sorted_alerts.end(), 
						KisAlert_Sort_Time());
			break;
		case alertsort_latest:
			stable_sort(sorted_alerts.begin(), sorted_alerts.end(), 
						KisAlert_Sort_TimeInv());
			break;
		case alertsort_type:
			stable_sort(sorted_alerts.begin(), sorted_alerts.end(), 
						KisAlert_Sort_Type());
			break;
		case alertsort_bssid:
			stable_sort(sorted_alerts.begin(), sorted_alerts.end(), 
						KisAlert_Sort_Bssid());
			break;
	}

	td.push_back("");
	td.push_back("");
	td.push_back("");

	for (unsigned int x = 0; x < sorted_alerts.size(); x++) {
		td[0] = 
			string(ctime((const time_t *) &(sorted_alerts[x]->tv.tv_sec))).substr(11, 8);
		td[1] = sorted_alerts[x]->alertname;
		td[2] = sorted_alerts[x]->text;
		alertlist->AddRow(k++, td);
	}

}

void Kis_AlertDetails_Panel::ButtonAction(Kis_Panel_Component *in_button) {

}

void Kis_AlertDetails_Panel::MenuAction(int opt) {

}

void Kis_AlertDetails_Panel::UpdateSortMenu(int mi) {
	menu->SetMenuItemChecked(mi_time, sort_mode == alertsort_time);
	menu->SetMenuItemChecked(mi_latest, sort_mode == alertsort_latest);
	menu->SetMenuItemChecked(mi_type, sort_mode == alertsort_type);
	menu->SetMenuItemChecked(mi_bssid, sort_mode == alertsort_bssid);
}

int Kis_AlertDetails_Panel::UpdateSortPrefs() {
	string sort;

	if ((sort = kpinterface->prefs->FetchOpt("ALERTLIST_SORT")) == "") {
		sort = "latest";
		kpinterface->prefs->SetOpt("ALERTLIST_SORT", sort, 1);
	}

	if (kpinterface->prefs->FetchOptDirty("ALERTLIST_SORT") == 0)
		return 0;

	kpinterface->prefs->SetOptDirty("ALERTLIST_SORT", 0);

	sort = StrLower(sort);

	if (sort == "latest")
		sort_mode = alertsort_latest;
	if (sort == "time")
		sort_mode = alertsort_time;
	if (sort == "type")
		sort_mode = alertsort_type;
	if (sort == "bssid")
		sort_mode = alertsort_bssid;
	else
		sort_mode = alertsort_latest;

	return 1;
}

#endif