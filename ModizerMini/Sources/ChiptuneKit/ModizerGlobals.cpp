//
//  ModizerGlobals.cpp
//  ModizerMini
//
//  The decoders in this repository were patched by Modizer to report data
//  through a handful of globals that normally live in ModizMusicPlayer.mm.
//  They are defined here so the same libraries can be reused.
//

extern "C" {

char bundledirectory[2048] = {0};
char mod_message[4096] = {0};
int mod_message_updated = 0;

int mSIDSeekInProgress = 0;
int sid_v4 = 0;
int m_genMasterVol = 128;
char bundlePath[1024] = {0};
unsigned long long organya_mute_mask = 0;
int pmd_real_tracks_used = 0;
signed char pmd_system_voice_idx[3] = {0,0,0};
signed char pmd_system_voice_nb[3] = {0,0,0};
int m_sid_chipId = 0;
int m_sid_chipNb = 0;
int mdz_ompt_hasReachEnd = 0;

}
