#define __MPKLIB_SELF__
#include "MpkLib.h"
#include "SCommon.h"

/*****************************************************************************/
/* Local functions                                                           */
/*****************************************************************************/

static BOOL OpenLocalFile(const char * szFileName, HANDLE * phFile)
{
    TMPKFile * hf = NULL;
    HANDLE hFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);

    if(hFile != INVALID_HANDLE_VALUE)
    {
        // Allocate and initialize file handle
        DWORD nHandleSize = sizeof(TMPKFile) + strlen(szFileName); 
        if((hf = (TMPKFile *)ALLOCMEM(char, nHandleSize)) != NULL)
        {
            memset(hf, 0, nHandleSize);
            strcpy(hf->szFileName, szFileName);
            hf->hFile = hFile;
            *phFile   = hf;
            return TRUE;
        }
        else
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    *phFile = NULL;
    return FALSE;
}

static void FreeMPKFile(TMPKFile *& hf)
{
    if(hf != NULL)
    {
        if(hf->hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hf->hFile);
        if(hf->pdwBlockPos != NULL)
            FREEMEM(hf->pdwBlockPos);
        if(hf->pbFileBuffer != NULL)
            FREEMEM(hf->pbFileBuffer);
        FREEMEM(hf);
        hf = NULL;
    }
}

/*****************************************************************************/
/* Public functions                                                          */
/*****************************************************************************/

//-----------------------------------------------------------------------------
// SFileEnumLocales enums all locale versions within MPK. 
// Functions fills all available language identifiers on a file into the buffer
// pointed by plcLocales. There must be enough entries to copy the localed,
// otherwise the function returns ERROR_INSUFFICIENT_BUFFER.

int WINAPI SFileEnumLocales(HANDLE hMPK, const char * szFileName, LCID * plcLocales, DWORD * pdwMaxLocales)
{
    TMPKArchive * ha = (TMPKArchive *)hMPK;
    TMPKHash * pHash = NULL;
    TMPKHash * pHashEnd = NULL;
    DWORD dwLocales = 0;
    int nError = ERROR_SUCCESS;

    // Test the parameters
    if(nError == ERROR_SUCCESS)
    {
        if(!IsValidMpkHandle(ha) || pdwMaxLocales == NULL)
            nError = ERROR_INVALID_PARAMETER;
        if((DWORD)szFileName < 0x00010000 && (DWORD)szFileName > ha->pHeader->dwBlockTableSize)
            nError = ERROR_INVALID_PARAMETER;
        if((DWORD)szFileName > 0x00010000 && *szFileName == 0)
            nError = ERROR_INVALID_PARAMETER;
    }

    // Retrieve the hash entry for the required file
    if(nError == ERROR_SUCCESS)
    {
        pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;

        if((DWORD)szFileName < 0x00010000)
        {
            for(pHash = ha->pHashTable; pHash < pHashEnd; pHash++)
            {
                if(pHash->dwBlockIndex == (DWORD)szFileName)
                    break;
            }
            if(pHash == pHashEnd)
                pHash = NULL;
        }
        else
            pHash = GetHashEntry(ha, szFileName);
    }

    // If the file was not found, sorry
    if(nError == ERROR_SUCCESS)
    {
        if(pHash == NULL)
            nError = ERROR_FILE_NOT_FOUND;
    }

    // Count the entries which correspond to the same file name
    if(nError == ERROR_SUCCESS)
    {
        TMPKHash * pSaveHash = pHash;
        DWORD dwName1 = pHash->dwName1;
        DWORD dwName2 = pHash->dwName2;

        // For nameless access, return 1 locale always
        if((DWORD)szFileName < 0x00010000)
            dwLocales++;
        else
        {
            while(pHash < pHashEnd && pHash->dwName1 == dwName1 && pHash->dwName2 == dwName2)
            {
                dwLocales++;
                pHash++;
            }
        }

        pHash = pSaveHash;
    }

    // Test if there is enough space to copy the locales
    if(nError == ERROR_SUCCESS)
    {
        DWORD dwMaxLocales = *pdwMaxLocales;

        *pdwMaxLocales = dwLocales;
        if(dwMaxLocales < dwLocales)
            nError = ERROR_INSUFFICIENT_BUFFER;
    }

    // Fill all the locales
    if(nError == ERROR_SUCCESS)
    {
        for(DWORD i = 0; i < dwLocales; i++, pHash++)
            *plcLocales++ = (LCID)pHash->lcLocale;
    }
    return nError;
}

//-----------------------------------------------------------------------------
// SFileHasFile
//
//   hMPK          - Handle of opened MPK archive
//   szFileName    - Name of file to look for

BOOL WINAPI SFileHasFile(HANDLE hMPK, char * szFileName)
{
    TMPKArchive * ha = (TMPKArchive *)hMPK;
    int nError = ERROR_SUCCESS;

    if(nError == ERROR_SUCCESS)
    {
        if(ha == NULL)
            nError = ERROR_INVALID_PARAMETER;
        if(*szFileName == 0)
            nError = ERROR_INVALID_PARAMETER;
    }

    // Prepare the file opening
    if(nError == ERROR_SUCCESS)
    {
        if(GetHashEntryEx(ha, szFileName, lcLocale) == NULL)
        {
            nError = ERROR_FILE_NOT_FOUND;
        }
    }

    // Cleanup
    if(nError != ERROR_SUCCESS)
    {
        SetLastError(nError);
    }

    return (nError == ERROR_SUCCESS);
}


//-----------------------------------------------------------------------------
// SFileOpenFileEx
//
//   hMPK          - Handle of opened MPK archive
//   szFileName    - Name of file to open
//   dwSearchScope - Where to search
//   phFile        - Pointer to store opened file handle

BOOL WINAPI SFileOpenFileEx(HANDLE hMPK, const char * szFileName, DWORD dwSearchScope, HANDLE * phFile)
{
    TMPKArchive * ha = (TMPKArchive *)hMPK;
    TMPKFile    * hf = NULL;
    TMPKHash    * pHash  = NULL;        // Hash table index
    TMPKBlock   * pBlock = NULL;        // File block
    DWORD dwHashIndex  = 0;             // Hash table index
    DWORD dwBlockIndex = (DWORD)-1;     // Found table index
    int nHandleSize = 0;                // Memory space necessary to allocate TMPKHandle
    int nError = ERROR_SUCCESS;

    if(nError == ERROR_SUCCESS)
    {
        if(ha == NULL && dwSearchScope == SFILE_OPEN_FROM_MPK)
            nError = ERROR_INVALID_PARAMETER;
        if(phFile == NULL)
            nError = ERROR_INVALID_PARAMETER;
        if((DWORD)szFileName < 0x00010000 && (DWORD)szFileName > ha->pHeader->dwBlockTableSize)
            nError = ERROR_INVALID_PARAMETER;
        if((DWORD)szFileName > 0x00010000 && *szFileName == 0)
            nError = ERROR_INVALID_PARAMETER;
    }

    // Prepare the file opening
    if(nError == ERROR_SUCCESS)
    {
        // When the file is given by number, ...
        if((DWORD)szFileName < 0x00010000)
        {
            TMPKHash * pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;

            // Set handle size to be sizeof(TMPKFile) + length of FileXXXXXXXX.xxx
            nHandleSize  = sizeof(TMPKFile) + 20;
            for(pHash = ha->pHashTable; pHash < pHashEnd; pHash++)
            {
                if((DWORD)szFileName == pHash->dwBlockIndex)
                {
                    dwHashIndex  = pHash - ha->pHashTable;
                    dwBlockIndex = pHash->dwBlockIndex;
                    break;
                }
            }
        }
        else
        {
            // If we have to open a disk file
            if(dwSearchScope == SFILE_OPEN_LOCAL_FILE)
                return OpenLocalFile(szFileName, phFile);

            nHandleSize = sizeof(TMPKFile) + strlen(szFileName);
            if((pHash = GetHashEntryEx(ha, szFileName, lcLocale)) != NULL)
            {
                dwHashIndex  = pHash - ha->pHashTable;
                dwBlockIndex = pHash->dwBlockIndex;
            }
        }
    }

    // Get block index from file name and test it
    if(nError == ERROR_SUCCESS)
    {
        // If index was not found, or is greater than number of files, exit.
        if(dwBlockIndex == (DWORD)-1 || dwBlockIndex > ha->pHeader->dwBlockTableSize)
            nError = ERROR_FILE_NOT_FOUND;
    }

    // Get block and test if the file was not already deleted.
    if(nError == ERROR_SUCCESS)
    {
        pBlock = ha->pBlockTable + dwBlockIndex;
        if(pBlock->dwFilePos > (ha->pHeader->dwArchiveSize + ha->dwMpkPos) ||
           pBlock->dwCSize   > ha->pHeader->dwArchiveSize)
            nError = ERROR_FILE_CORRUPT;
        if((pBlock->dwFlags & MPK_FILE_EXISTS) == 0)
            nError = ERROR_FILE_NOT_FOUND;
        if(pBlock->dwFlags & ~MPK_FILE_VALID_FLAGS)
            nError = ERROR_NOT_SUPPORTED;
    }

    // Allocate file handle
    if(nError == ERROR_SUCCESS)
    {
        if((hf = (TMPKFile *)ALLOCMEM(char, nHandleSize)) == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Initialize file handle
    if(nError == ERROR_SUCCESS)
    {
        memset(hf, 0, nHandleSize);
        hf->hFile   = INVALID_HANDLE_VALUE;
        hf->ha      = ha;
        hf->pBlock  = pBlock;
        hf->nBlocks = (hf->pBlock->dwFSize + ha->dwBlockSize - 1) / ha->dwBlockSize;
        hf->pHash   = pHash;
        
        // Fill non-Storm items
        hf->dwHashIndex = dwHashIndex;
        hf->dwFileIndex = dwBlockIndex; 

        // Allocate buffers for decompression.
        if(hf->pBlock->dwFlags & MPK_FILE_COMPRESSED)
        {
            // Allocate buffer for block positions. At the begin of file are stored
            // DWORDs holding positions of each block relative from begin of file in the archive
            if((hf->pdwBlockPos = ALLOCMEM(DWORD, hf->nBlocks + 1)) == NULL)
                nError = ERROR_NOT_ENOUGH_MEMORY;
        }
        
        // Decrypt file seed. Cannot be used if the file is given by index
        if((DWORD)szFileName > 0x00010000)
        {
            if(hf->pBlock->dwFlags & MPK_FILE_ENCRYPTED)
            {
                const char * szTemp = strrchr(szFileName, '\\');

                strcpy(hf->szFileName, szFileName);
                if(szTemp != NULL)
                    szFileName = szTemp + 1;
                hf->dwSeed1 = DecryptFileSeed((char *)szFileName);

                if(hf->pBlock->dwFlags & MPK_FILE_FIXSEED)
                    hf->dwSeed1 = (hf->dwSeed1 + (hf->pBlock->dwFilePos - ha->dwMpkPos)) ^ hf->pBlock->dwFSize;
            }
        }
        else
        {
            // If the file is encrypted and not compressed, we cannot detect the file seed
            if(SFileGetFileName(hf, hf->szFileName) == FALSE)
                nError = GetLastError();
        }
    }

    // Cleanup
    if(nError != ERROR_SUCCESS)
    {
        FreeMPKFile(hf);
        SetLastError(nError);
    }

    *phFile = hf;
    return (nError == ERROR_SUCCESS);
}

//-----------------------------------------------------------------------------
// BOOL SFileCloseFile(HANDLE hFile);

BOOL WINAPI SFileCloseFile(HANDLE hFile)
{
    TMPKFile * hf = (TMPKFile *)hFile;
    
    if(!IsValidFileHandle(hf))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // Set the last accessed file in the archive
    if(hf->ha != NULL)
        hf->ha->pLastFile = NULL;

    // Free the structure
    FreeMPKFile(hf);
    return TRUE;
}
