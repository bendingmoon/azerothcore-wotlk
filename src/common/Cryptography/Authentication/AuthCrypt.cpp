/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "AuthCrypt.h"
#include "Errors.h"
#include "HMAC.h"

void AuthCrypt::Init(SessionKey const& K)
{
    uint8 ServerEncryptionKey[] = { 0x12, 0xC2, 0xA2, 0xC4, 0xE2, 0x98, 0xE6, 0xBA, 0x16, 0xDC, 0xC2, 0x83, 0x46, 0x92, 0x56, 0x58 };
    _serverEncrypt.Init(Acore::Crypto::HMAC_SHA1::GetDigestOf(ServerEncryptionKey, K));

    uint8 ServerDecryptionKey[] = { 0xD2, 0xC3, 0x82, 0x4C, 0xD6, 0xBE, 0xD8, 0xC5, 0x36, 0x4C, 0x63, 0xEE, 0x3F, 0x53, 0x68, 0xDE };
    _clientDecrypt.Init(Acore::Crypto::HMAC_SHA1::GetDigestOf(ServerDecryptionKey, K));

    // Drop first 1024 bytes, as WoW uses ARC4-drop1024.
    std::array<uint8, 1024> syncBuf{};
    _serverEncrypt.UpdateData(syncBuf);
    _clientDecrypt.UpdateData(syncBuf);

    _initialized = true;
}

void AuthCrypt::DecryptRecv(uint8* data, std::size_t len)
{
    ASSERT(_initialized);
    _clientDecrypt.UpdateData(data, len);
}

void AuthCrypt::EncryptSend(uint8* data, std::size_t len)
{
    ASSERT(_initialized);
    _serverEncrypt.UpdateData(data, len);
}
