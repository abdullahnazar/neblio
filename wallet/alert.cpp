//
// Alert system
//

#include <algorithm>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/foreach.hpp>
#include <map>

#include "alert.h"
#include "key.h"
#include "net.h"
#include "sync.h"
#include "ui_interface.h"

using namespace std;

map<uint256, CAlert> mapAlerts;
CCriticalSection     cs_mapAlerts;

static const char* pszMainKey = "046df586b596db22eda44a90b08fbaab100dff97612d3eb32cee236ea385cb09e5e05fc"
                                "0b0d2ec278ac0ac97daba8201508be27b4de780be06d447217037c6d082";

// TestNet alerts pubKey
static const char* pszTestKey = "04da59da7f2e1c9d0f575187065930361ad09751f7a8ccae25f0ab9ebbd479c0cda65a8"
                                "ae0415a4a64bac46f79a4cd67bdb0925871855db3227969005361beaf21";

void CUnsignedAlert::SetNull()
{
    nVersion    = 1;
    nRelayUntil = 0;
    nExpiration = 0;
    nID         = 0;
    nCancel     = 0;
    setCancel.clear();
    nMinVer = 0;
    nMaxVer = 0;
    setSubVer.clear();
    nPriority = 0;

    strComment.clear();
    strStatusBar.clear();
    strReserved.clear();
}

std::string CUnsignedAlert::ToString() const
{
    std::string strSetCancel;
    for (int n : setCancel)
        strSetCancel += strprintf("%d ", n);
    std::string strSetSubVer;
    for (std::string str : setSubVer)
        strSetSubVer += "\"" + str + "\" ";
    return strprintf("CAlert(\n"
                     "    nVersion     = %d\n"
                     "    nRelayUntil  = %" PRId64 "\n"
                     "    nExpiration  = %" PRId64 "\n"
                     "    nID          = %d\n"
                     "    nCancel      = %d\n"
                     "    setCancel    = %s\n"
                     "    nMinVer      = %d\n"
                     "    nMaxVer      = %d\n"
                     "    setSubVer    = %s\n"
                     "    nPriority    = %d\n"
                     "    strComment   = \"%s\"\n"
                     "    strStatusBar = \"%s\"\n"
                     ")\n",
                     nVersion, nRelayUntil, nExpiration, nID, nCancel, strSetCancel.c_str(), nMinVer,
                     nMaxVer, strSetSubVer.c_str(), nPriority, strComment.c_str(), strStatusBar.c_str());
}

void CUnsignedAlert::print() const { printf("%s", ToString().c_str()); }

void CAlert::SetNull()
{
    CUnsignedAlert::SetNull();
    vchMsg.clear();
    vchSig.clear();
}

bool CAlert::IsNull() const { return (nExpiration == 0); }

uint256 CAlert::GetHash() const { return Hash(this->vchMsg.begin(), this->vchMsg.end()); }

bool CAlert::IsInEffect() const { return (GetAdjustedTime() < nExpiration); }

bool CAlert::Cancels(const CAlert& alert) const
{
    if (!IsInEffect())
        return false; // this was a no-op before 31403
    return (alert.nID <= nCancel || setCancel.count(alert.nID));
}

bool CAlert::AppliesTo(int nVersion, std::string strSubVerIn) const
{
    // TODO: rework for client-version-embedded-in-strSubVer ?
    return (IsInEffect() && nMinVer <= nVersion && nVersion <= nMaxVer &&
            (setSubVer.empty() || setSubVer.count(strSubVerIn)));
}

bool CAlert::AppliesToMe() const
{
    return AppliesTo(PROTOCOL_VERSION,
                     FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<std::string>()));
}

bool CAlert::RelayTo(CNode* pnode) const
{
    if (!IsInEffect())
        return false;
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second) {
        if (AppliesTo(pnode->nVersion, pnode->strSubVer) || AppliesToMe() ||
            GetAdjustedTime() < nRelayUntil) {
            pnode->PushMessage("alert", *this);
            return true;
        }
    }
    return false;
}

bool CAlert::CheckSignature() const
{
    CKey key;
    if (!key.SetPubKey(ParseHex(IsTestnet() ? pszTestKey : pszMainKey)))
        return error("CAlert::CheckSignature() : SetPubKey failed");
    if (!key.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig))
        return error("CAlert::CheckSignature() : verify signature failed");

    // Now unserialize the data
    CDataStream sMsg(vchMsg, SER_NETWORK, PROTOCOL_VERSION);
    sMsg >> *(CUnsignedAlert*)this;
    return true;
}

CAlert CAlert::getAlertByHash(const uint256& hash)
{
    CAlert retval;
    {
        LOCK(cs_mapAlerts);
        map<uint256, CAlert>::iterator mi = mapAlerts.find(hash);
        if (mi != mapAlerts.end())
            retval = mi->second;
    }
    return retval;
}

bool CAlert::ProcessAlert(bool fThread)
{
    if (!CheckSignature())
        return false;
    if (!IsInEffect())
        return false;

    // alert.nID=max is reserved for if the alert key is
    // compromised. It must have a pre-defined message,
    // must never expire, must apply to all versions,
    // and must cancel all previous
    // alerts or it will be ignored (so an attacker can't
    // send an "everything is OK, don't panic" version that
    // cannot be overridden):
    int maxInt = std::numeric_limits<int>::max();
    if (nID == maxInt) {
        if (!(nExpiration == maxInt && nCancel == (maxInt - 1) && nMinVer == 0 && nMaxVer == maxInt &&
              setSubVer.empty() && nPriority == maxInt &&
              strStatusBar == "URGENT: Alert key compromised, upgrade required"))
            return false;
    }

    {
        LOCK(cs_mapAlerts);
        // Cancel previous alerts
        for (map<uint256, CAlert>::iterator mi = mapAlerts.begin(); mi != mapAlerts.end();) {
            const CAlert& alert = (*mi).second;
            if (Cancels(alert)) {
                printf("cancelling alert %d\n", alert.nID);
                uiInterface.NotifyAlertChanged((*mi).first, CT_DELETED);
                mapAlerts.erase(mi++);
            } else if (!alert.IsInEffect()) {
                printf("expiring alert %d\n", alert.nID);
                uiInterface.NotifyAlertChanged((*mi).first, CT_DELETED);
                mapAlerts.erase(mi++);
            } else
                mi++;
        }

        // Check if this alert has been cancelled
        for (PAIRTYPE(const uint256, CAlert) & item : mapAlerts) {
            const CAlert& alert = item.second;
            if (alert.Cancels(*this)) {
                printf("alert already cancelled by %d\n", alert.nID);
                return false;
            }
        }

        // Add to mapAlerts
        mapAlerts.insert(make_pair(GetHash(), *this));
        // Notify UI and -alertnotify if it applies to me
        if (AppliesToMe()) {
            uiInterface.NotifyAlertChanged(GetHash(), CT_NEW);
            std::string strCmd = GetArg("-alertnotify", "");
            if (!strCmd.empty()) {
                // Alert text should be plain ascii coming from a trusted source, but to
                // be safe we first strip anything not in safeChars, then add single quotes around
                // the whole string before passing it to the shell:
                std::string singleQuote("'");
                // safeChars chosen to allow simple messages/URLs/email addresses, but avoid anything
                // even possibly remotely dangerous like & or >
                std::string safeChars(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890 .,;_/:?@");
                std::string safeStatus;
                for (std::string::size_type i = 0; i < strStatusBar.size(); i++) {
                    if (safeChars.find(strStatusBar[i]) != std::string::npos)
                        safeStatus.push_back(strStatusBar[i]);
                }
                safeStatus = singleQuote + safeStatus + singleQuote;
                boost::replace_all(strCmd, "%s", safeStatus);

                if (fThread)
                    boost::thread t(runCommand, strCmd); // thread runs free
                else
                    runCommand(strCmd);
            }
        }
    }

    printf("accepted alert %d, AppliesToMe()=%d\n", nID, AppliesToMe());
    return true;
}
