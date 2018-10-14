// Copyright (c) 2009-2012 The Bitcoin Developers.
// Authored by Google, Inc.
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LMDB_H
#define BITCOIN_LMDB_H

#include "main.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "liblmdb/lmdb.h"

#include "ntp1/ntp1transaction.h"

#define ENABLE_AUTO_RESIZE

extern std::unique_ptr<MDB_env, std::function<void(MDB_env*)>> dbEnv;
extern std::unique_ptr<MDB_dbi, std::function<void(MDB_dbi*)>> txdb; // global pointer for lmdb object instance

constexpr static float DB_RESIZE_PERCENT = 0.9f;

#if defined(__arm__)
// force a value so it can compile with 32-bit ARM
constexpr static uint64_t DB_DEFAULT_MAPSIZE = UINT64_C(1) << 31;
#else
#if defined(ENABLE_AUTO_RESIZE)
constexpr static uint64_t DB_DEFAULT_MAPSIZE = UINT64_C(1) << 30;
#else
constexpr static uint64_t DB_DEFAULT_MAPSIZE = UINT64_C(1) << 33;
#endif
#endif

const std::string LMDB_MAINDB = "maindb";

class CTxDB;

void lmdb_resized(MDB_env* env);

inline int lmdb_txn_begin(MDB_env *env, MDB_txn *parent, unsigned int flags, MDB_txn **txn)
{
  int res = mdb_txn_begin(env, parent, flags, txn);
  if (res == MDB_MAP_RESIZED) {
    lmdb_resized(env);
    res = mdb_txn_begin(env, parent, flags, txn);
  }
  return res;
}

struct mdb_txn_safe
{
    mdb_txn_safe(const bool check = true);
    ~mdb_txn_safe();

    void commit(std::string message = "");
    void commitIfValid(std::string message = "");

    // This should only be needed for batch transaction which must be ensured to
    // be aborted before mdb_env_close, not after. So we can't rely on
    // BlockchainLMDB destructor to call mdb_txn_safe destructor, as that's too late
    // to properly abort, since mdb_env_close would have been called earlier.
    void abort();
    void abortIfValid();
    void uncheck();

    operator MDB_txn*() { return m_txn; }

    operator MDB_txn**() { return &m_txn; }

    MDB_txn* rawPtr() const { return m_txn; }

    uint64_t num_active_tx() const;

    static void prevent_new_txns();
    static void wait_no_active_txns();
    static void allow_new_txns();

    MDB_txn*                     m_txn;
    bool                         m_batch_txn = false;
    bool                         m_check;
    static std::atomic<uint64_t> num_active_txns;

    // could use a mutex here, but this should be sufficient.
    static std::atomic_flag creation_gate;
};

// Class that provides access to a LevelDB. Note that this class is frequently
// instantiated on the stack and then destroyed again, so instantiation has to
// be very cheap. Unfortunately that means, a CTxDB instance is actually just a
// wrapper around some global state.
//
// A LevelDB is a key/value store that is optimized for fast usage on hard
// disks. It prefers long read/writes to seeks and is based on a series of
// sorted key/value mapping files that are stacked on top of each other, with
// newer files overriding older files. A background thread compacts them
// together when too many files stack up.
//
// Learn more: http://code.google.com/p/leveldb/
class CTxDB
{
public:
    static boost::filesystem::path DB_DIR;

    CTxDB(const char* pszMode = "r+");
    ~CTxDB()
    {
        // Note that this is not the same as Close() because it deletes only
        // data scoped to this TxDB object.
        pdb = nullptr;
    }

    // Destroys the underlying shared global state accessed by this TxDB.
    void Close();

    static const int WriteReps = 32;

private:
    MDB_dbi* pdb; // Points to the global instance.

    // A batch stores up writes and deletes for atomic application. When this
    // field is non-NULL, writes/deletes go there instead of directly to disk.
    mdb_txn_safe activeBatch;
    bool         fReadOnly;
    int          nVersion;

protected:
public:

    static void __deleteDb();

    // Returns true and sets (value,false) if activeBatch contains the given key
    // or leaves value alone and sets deleted = true if activeBatch contains a
    // delete for it.
    //    bool ScanBatch(const CDataStream& key, std::string* value, bool* deleted) const;

    template <typename K, typename T>
    bool Read(const K& key, T& value)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        std::string strValue;

        bool readFromDb = true;
        //        if (activeBatch) {
        //            // First we must search for it in the currently pending set of
        //            // changes to the db. If not found in the batch, go on to read disk.
        //            bool deleted = false;
        //            readFromDb   = ScanBatch(ssKey, &strValue, &deleted) == false;
        //            if (deleted) {
        //                return false;
        //            }
        //        }
        mdb_txn_safe localTxn;
        if (!activeBatch.rawPtr()) {
            localTxn = mdb_txn_safe();
            lmdb_txn_begin(dbEnv.get(), nullptr, MDB_RDONLY, localTxn);
        }

        // only one of them should be active
        assert(localTxn.rawPtr() == nullptr || activeBatch.rawPtr() == nullptr);

        if (readFromDb) {
            MDB_val kS = {ssKey.size(), (void*)ssKey.str().c_str()};
            MDB_val vS;
            if (auto ret = mdb_get((localTxn.rawPtr() ? localTxn : activeBatch), *pdb, &kS, &vS)) {
                if (ret == MDB_NOTFOUND) {
                    printf("Failed to read lmdb key %s as it doesn't exist\n", ssKey.str().c_str());
                } else {
                    printf("Failed to read lmdb key with an unknown error of code %i", ret);
                }
                if (localTxn.rawPtr()) {
                    localTxn.abort();
                }
                return false;
            }
            strValue.assign(static_cast<const char*>(vS.mv_data), vS.mv_size);
            //            leveldb::Status status = pdb->Get(leveldb::ReadOptions(), ssKey.str(),
            //            &strValue); if (!status.ok()) {
            //                if (status.IsNotFound())
            //                    return false;
            //                // Some unexpected error.
            //                printf("lmdb read failure: %s\n", status.ToString().c_str());
            //                return false;
            //            }
        }
        // Unserialize value
        try {
            CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK,
                                CLIENT_VERSION);
            ssValue >> value;
        } catch (std::exception& e) {
            printf("Failed to deserialized data when reading for key %s\n", ssKey.str().c_str());
            return false;
        }
        return true;
    }

    template <typename K, typename T>
    bool Write(const K& key, const T& value)
    {
        if (fReadOnly)
            assert(!"Write called on database in read-only mode");

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.reserve(10000);
        ssValue << value;

        if (CTxDB::need_resize()) {
            printf("LMDB memory map needs to be resized, doing that now.\n");
            CTxDB::do_resize();
        }

        //        if (activeBatch) {
        //            activeBatch->Put(ssKey.str(), ssValue.str());
        //            return true;
        //        }
        mdb_txn_safe localTxn;
        if (!activeBatch.rawPtr()) {
            localTxn = mdb_txn_safe();
            lmdb_txn_begin(dbEnv.get(), nullptr, 0, localTxn);
        }

        // only one of them should be active
        assert(localTxn.rawPtr() == nullptr || activeBatch.rawPtr() == nullptr);

        MDB_val kS = {ssKey.size(), (void*)ssKey.str().c_str()};
        MDB_val vS = {ssValue.size(), (void*)ssValue.str().c_str()};

        if (auto ret = mdb_put((localTxn.rawPtr() ? localTxn : activeBatch), *pdb, &kS, &vS, 0)) {
            if (ret == MDB_MAP_FULL) {
                printf("Failed to write key %s with lmdb, MDB_MAP_FULL\n", ssKey.str().c_str());
            } else {
                printf("Failed to write key with lmdb, unknown reason\n");
            }
            if (localTxn.rawPtr()) {
                localTxn.abort();
            }
            return false;
        }
        //        leveldb::Status status = pdb->Put(leveldb::WriteOptions(), ssKey.str(), ssValue.str());
        //        if (!status.ok()) {
        //            printf("LevelDB write failure: %s\n", status.ToString().c_str());
        //            return false;
        //        }
        localTxn.commitIfValid("Tx while writing");
        return true;
    }

    template <typename K>
    bool Erase(const K& key)
    {
        if (!pdb)
            return false;
        if (fReadOnly)
            assert(!"Erase called on database in read-only mode");

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;

        mdb_txn_safe localTxn;
        if (!activeBatch.rawPtr()) {
            localTxn = mdb_txn_safe();
            lmdb_txn_begin(dbEnv.get(), nullptr, 0, localTxn);
        }

        // only one of them should be active
        assert(localTxn.rawPtr() == nullptr || activeBatch.rawPtr() == nullptr);

        MDB_val kS = {ssKey.size(), (void*)ssKey.str().c_str()};
        MDB_val vS;

        if (auto ret = mdb_del((localTxn.rawPtr() ? localTxn : activeBatch), *pdb, &kS, &vS)) {
            printf("Failed to delete entry with key %s with lmdb\n", ssKey.str().c_str());
            if (localTxn.rawPtr()) {
                localTxn.abort();
            }
            return false;
        }

        //        if (activeBatch) {
        //            activeBatch->Delete(ssKey.str());
        //            return true;
        //        }
        //        leveldb::Status status = pdb->Delete(leveldb::WriteOptions(), ssKey.str());
        //        return (status.ok() || status.IsNotFound());
        localTxn.commitIfValid("Tx while erasing");
        return true;
    }

    template <typename K>
    bool Exists(const K& key)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        std::string unused;

        mdb_txn_safe localTxn;
        if (!activeBatch.rawPtr()) {
            localTxn = mdb_txn_safe();
            lmdb_txn_begin(dbEnv.get(), nullptr, MDB_RDONLY, localTxn);
        }

        // only one of them should be active
        assert(localTxn.rawPtr() == nullptr || activeBatch.rawPtr() == nullptr);

        MDB_val kS = {ssKey.size(), (void*)ssKey.str().c_str()};
        MDB_val vS;
        if (auto ret = mdb_get((localTxn.rawPtr() ? localTxn : activeBatch), *pdb, &kS, &vS)) {
            if (localTxn.rawPtr()) {
                localTxn.abort();
            }
            if (ret == MDB_NOTFOUND) {
                return false;
            } else {
                printf("Failed to check whether key %s exists with an unknown error of code %i\n",
                       ssKey.str().c_str(), ret);
            }
            return false;
        } else {
            return true;
        }

        //        if (activeBatch) {
        //            bool deleted;
        //            if (ScanBatch(ssKey, &unused, &deleted) && !deleted) {
        //                return true;
        //            }
        //        }

        //        leveldb::Status status = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &unused);
        //        return status.IsNotFound() == false;
    }

public:
    inline static void lmdb_db_open(MDB_txn* txn, const char* name, int flags, MDB_dbi& dbi,
                                    const std::string& error_string)
    {
        if (int res = mdb_dbi_open(txn, name, flags, &dbi)) {
            printf("Error opening lmdb database. Error code: %d\n", res);
            throw std::runtime_error(error_string + ": " + std::to_string(res));
        }
    }

    static bool need_resize(uint64_t threshold_size = 0);
    void        do_resize(uint64_t increase_size = 0);
    bool        TxnBegin();
    bool        TxnCommit();
    bool        TxnAbort()
    {
        activeBatch.abort();
        return true;
    }

    bool ReadVersion(int& nVersion)
    {
        nVersion = 0;
        return Read(std::string("version"), nVersion);
    }

    bool WriteVersion(int nVersion) { return Write(std::string("version"), nVersion); }

    bool ReadTxIndex(uint256 hash, CTxIndex& txindex);
    bool UpdateTxIndex(uint256 hash, const CTxIndex& txindex);
    bool ReadNTP1TxIndex(uint256 hash, DiskNTP1TxPos& txindex);
    bool WriteNTP1TxIndex(uint256 hash, const DiskNTP1TxPos& txindex);
    bool AddTxIndex(const CTransaction& tx, const CDiskTxPos& pos, int nHeight);
    bool EraseTxIndex(const CTransaction& tx);
    bool ContainsTx(uint256 hash);
    bool ContainsNTP1Tx(uint256 hash);
    bool ReadDiskTx(uint256 hash, CTransaction& tx, CTxIndex& txindex);
    bool ReadDiskTx(uint256 hash, CTransaction& tx);
    bool ReadDiskTx(COutPoint outpoint, CTransaction& tx, CTxIndex& txindex);
    bool ReadDiskTx(COutPoint outpoint, CTransaction& tx);
    bool WriteBlockIndex(const CDiskBlockIndex& blockindex);
    bool ReadHashBestChain(uint256& hashBestChain);
    bool WriteHashBestChain(uint256 hashBestChain);
    bool ReadBestInvalidTrust(CBigNum& bnBestInvalidTrust);
    bool WriteBestInvalidTrust(CBigNum bnBestInvalidTrust);
    bool ReadSyncCheckpoint(uint256& hashCheckpoint);
    bool WriteSyncCheckpoint(uint256 hashCheckpoint);
    bool ReadCheckpointPubKey(std::string& strPubKey);
    bool WriteCheckpointPubKey(const std::string& strPubKey);
    bool LoadBlockIndex();

    void init_blockindex(bool fRemoveOld = false);

private:
    bool LoadBlockIndexGuts();
};

#endif // BITCOIN_LMDB_H
