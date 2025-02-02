//
// Created by Martin Steinegger on 2019-01-04.
//

#include "QueryMatcher.h"
#include "PrefilteringIndexReader.h"
#include "NucleotideMatrix.h"
#include "SubstitutionMatrix.h"
#include "ReducedMatrix.h"
#include "Command.h"
#include "kmersearch.h"
#include "Debug.h"
#include "LinsearchIndexReader.h"
#include "Timer.h"
#include "KmerIndex.h"
#include "FileUtil.h"

#include "omptl/omptl_algorithm"

#ifndef SIZE_T_MAX
#define SIZE_T_MAX ((size_t) -1)
#endif

KmerSearch::ExtractKmerAndSortResult KmerSearch::extractKmerAndSort(size_t totalKmers, size_t split, size_t splits, DBReader<unsigned int> & seqDbr,
                                                                 Parameters & par, BaseMatrix  * subMat, size_t KMER_SIZE, size_t chooseTopKmer, size_t pickNBest, bool adjustLength) {
    Debug(Debug::INFO) << "Generate k-mers list " << split <<"\n";

    size_t splitKmerCount = totalKmers;
    if(splits > 1){
        size_t memoryLimit;
        if (par.splitMemoryLimit > 0) {
            memoryLimit = static_cast<size_t>(par.splitMemoryLimit);
        } else {
            memoryLimit = static_cast<size_t>(Util::getTotalSystemMemory() * 0.9);
        }
        // we do not really know how much memory is needed. So this is our best choice
        splitKmerCount = (memoryLimit / sizeof(KmerPosition<short>));
    }

    KmerPosition<short> * hashSeqPair = initKmerPositionMemory<short>(splitKmerCount*pickNBest);
    Timer timer;
    size_t elementsToSort;
    if(pickNBest > 1){
        std::pair<size_t, size_t> ret = fillKmerPositionArray<Parameters::DBTYPE_HMM_PROFILE,short>(hashSeqPair, seqDbr, par, subMat, KMER_SIZE,
                                                                               chooseTopKmer, false, splits, split, pickNBest, false);
        elementsToSort = ret.first;
    } else if(Parameters::isEqualDbtype(seqDbr.getDbtype(), Parameters::DBTYPE_NUCLEOTIDES)){
        std::pair<size_t, size_t> ret = fillKmerPositionArray<Parameters::DBTYPE_NUCLEOTIDES,short>(hashSeqPair, seqDbr, par, subMat, KMER_SIZE,
                                                                               chooseTopKmer, false, splits, split, 1, adjustLength);
        elementsToSort = ret.first;
        KMER_SIZE = ret.second;
        Debug(Debug::INFO) << "\nAdjusted k-mer length " << KMER_SIZE << "\n";
    }else {
        std::pair<size_t, size_t> ret = fillKmerPositionArray<Parameters::DBTYPE_AMINO_ACIDS, short>(hashSeqPair, seqDbr, par, subMat, KMER_SIZE,
                                                                               chooseTopKmer, false, splits, split, 1, false);
        elementsToSort = ret.first;

    }
    Debug(Debug::INFO) << "\nTime for fill: " << timer.lap() << "\n";
    if(splits == 1){
        seqDbr.unmapData();
    }

    Debug(Debug::INFO) << "Sort kmer ... ";
    timer.reset();
    if(Parameters::isEqualDbtype(seqDbr.getDbtype(), Parameters::DBTYPE_NUCLEOTIDES)) {
        omptl::sort(hashSeqPair, hashSeqPair + elementsToSort, KmerPosition<short>::compareRepSequenceAndIdAndPosReverse);
    }else{
        omptl::sort(hashSeqPair, hashSeqPair + elementsToSort, KmerPosition<short>::compareRepSequenceAndIdAndPos);
    }


    Debug(Debug::INFO) << "Time for sort: " << timer.lap() << "\n";

    return ExtractKmerAndSortResult(elementsToSort, hashSeqPair, KMER_SIZE);
}

template <int TYPE>
void KmerSearch::writeResult(DBWriter & dbw, KmerPosition<short> *kmers, size_t kmerCount) {
    size_t repSeqId = SIZE_T_MAX;
    unsigned int prevHitId;
    char buffer[100];
    std::string prefResultsOutString;
    prefResultsOutString.reserve(100000000);
    for(size_t i = 0; i < kmerCount; i++) {
        size_t currId = kmers[i].kmer;
        int reverMask = 0;
        if(TYPE == Parameters::DBTYPE_NUCLEOTIDES){
            reverMask  = BIT_CHECK(kmers[i].kmer, 63) == false;
            currId = BIT_CLEAR(currId, 63);
        }
        if (repSeqId != currId) {
            if(repSeqId != SIZE_T_MAX){
                dbw.writeData(prefResultsOutString.c_str(), prefResultsOutString.length(), static_cast<unsigned int>(repSeqId), 0);
            }
            repSeqId = currId;
            prefResultsOutString.clear();
        }
//        std::cout << kmers[i].id << "\t" << kmers[i].pos << std::endl;
        // find maximal diagonal and top score
        short prevDiagonal;
        int cnt = 0;
        int bestDiagonalCnt = 0;
        int bestRevertMask = reverMask;
        short bestDiagonal = kmers[i].pos;
        int topScore = 0;
        unsigned int tmpCurrId = currId;

        unsigned int hitId;
        do {
            prevHitId = kmers[i].id;
            prevDiagonal = kmers[i].pos;

            cnt = (kmers[i].pos == prevDiagonal) ? cnt + 1 : 1 ;
            if(cnt > bestDiagonalCnt){
                bestDiagonalCnt = cnt;
                bestDiagonal = kmers[i].pos;
                bestRevertMask = reverMask;
            }
            topScore++;
            i++;
            hitId = kmers[i].id;
            tmpCurrId = kmers[i].kmer;
            if(TYPE == Parameters::DBTYPE_NUCLEOTIDES) {
                reverMask = BIT_CHECK(kmers[i].kmer, 63) == false;
                tmpCurrId = BIT_CLEAR(tmpCurrId, 63);

            }
        } while(hitId == prevHitId && currId == tmpCurrId && i < kmerCount);
        i--;

        hit_t h;
        h.seqId = prevHitId;
        bestRevertMask = (repSeqId == prevHitId) ? 0 : bestRevertMask;
        h.prefScore =  (bestRevertMask) ? -topScore : topScore;
        h.diagonal =  bestDiagonal;
        int len = QueryMatcher::prefilterHitToBuffer(buffer, h);
        prefResultsOutString.append(buffer, len);

    }
    // last element
    if(prefResultsOutString.size()>0){
        if(repSeqId != SIZE_T_MAX){
            dbw.writeData(prefResultsOutString.c_str(), prefResultsOutString.length(), static_cast<unsigned int>(repSeqId), 0);
        }
    }
}

template void KmerSearch::writeResult<0>(DBWriter & dbw, KmerPosition<short> *kmers, size_t kmerCount);
template void KmerSearch::writeResult<1>(DBWriter & dbw, KmerPosition<short> *kmers, size_t kmerCount);

int kmersearch(int argc, const char **argv, const Command &command) {
    Parameters &par = Parameters::getInstance();
    setLinearFilterDefault(&par);
    par.parseParameters(argc, argv, command, true, 0, MMseqsParameter::COMMAND_CLUSTLINEAR);
    int targetSeqType;
    int adjustedKmerSize = 0;
    bool isAdjustedKmerLen = false;
    if (Parameters::isEqualDbtype(FileUtil::parseDbType(par.db2.c_str()), Parameters::DBTYPE_INDEX_DB) == false) {
        Debug(Debug::ERROR) << "Create index before calling kmersearch with mmseqs createlinindex.\n";
        EXIT(EXIT_FAILURE);
    }

    DBReader<unsigned int> tidxdbr(par.db2.c_str(), par.db2Index.c_str(), par.threads, DBReader<unsigned int>::USE_INDEX|DBReader<unsigned int>::USE_DATA);
    tidxdbr.open(DBReader<unsigned int>::NOSORT);
    PrefilteringIndexData data = PrefilteringIndexReader::getMetadata(&tidxdbr);
    if(par.PARAM_K.wasSet){
        if(par.kmerSize != 0 && data.kmerSize != par.kmerSize){
            Debug(Debug::ERROR) << "Index was created with -k " << data.kmerSize << " but the prefilter was called with -k " << par.kmerSize << "!\n";
            Debug(Debug::ERROR) << "createindex -k " << par.kmerSize << "\n";
            EXIT(EXIT_FAILURE);
        }
    }
    if(par.PARAM_ALPH_SIZE.wasSet){
        if(data.alphabetSize != par.alphabetSize){
            Debug(Debug::ERROR) << "Index was created with --alph-size  " << data.alphabetSize << " but the prefilter was called with --alph-size " << par.alphabetSize << "!\n";
            Debug(Debug::ERROR) << "createindex --alph-size " << par.alphabetSize << "\n";
            EXIT(EXIT_FAILURE);
        }
    }
    if(par.PARAM_SPACED_KMER_MODE.wasSet){
        if(data.spacedKmer != par.spacedKmer){
            Debug(Debug::ERROR) << "Index was created with --spaced-kmer-mode " << data.spacedKmer << " but the prefilter was called with --spaced-kmer-mode " << par.spacedKmer << "!\n";
            Debug(Debug::ERROR) << "createindex --spaced-kmer-mode " << par.spacedKmer << "\n";
            EXIT(EXIT_FAILURE);
        }
    }
    par.kmerSize = data.kmerSize;
    par.alphabetSize = data.alphabetSize;
    targetSeqType = data.seqType;
    par.spacedKmer = (data.spacedKmer == 1) ? true : false;
    par.maxSeqLen = data.maxSeqLength;
    // Reuse the compBiasCorr field to store the adjustedKmerSize, It is not needed in the linsearch
    adjustedKmerSize = data.compBiasCorr;
    isAdjustedKmerLen = data.kmerSize != adjustedKmerSize;

    DBReader<unsigned int> queryDbr(par.db1.c_str(), par.db1Index.c_str(), par.threads, DBReader<unsigned int>::USE_INDEX|DBReader<unsigned int>::USE_DATA);
    queryDbr.open(DBReader<unsigned int>::NOSORT);
    int querySeqType = queryDbr.getDbtype();
    if (Parameters::isEqualDbtype(querySeqType, targetSeqType) == false) {
        Debug(Debug::ERROR) << "Dbtype of query and target database do not match !\n";
        EXIT(EXIT_FAILURE);
    }

    setKmerLengthAndAlphabet(par, queryDbr.getAminoAcidDBSize(), querySeqType);

    par.printParameters(command.cmd, argc, argv, *command.params);

    //queryDbr.readMmapedDataInMemory();
    const size_t KMER_SIZE = par.kmerSize;
    size_t chooseTopKmer = par.kmersPerSequence;

    // memoryLimit in bytes
    size_t memoryLimit;
    if (par.splitMemoryLimit > 0) {
        memoryLimit = par.splitMemoryLimit;
    } else {
        memoryLimit = static_cast<size_t>(Util::getTotalSystemMemory() * 0.9);
    }

    size_t totalKmers = computeKmerCount(queryDbr, KMER_SIZE, chooseTopKmer);
    size_t totalSizeNeeded = computeMemoryNeededLinearfilter<short>(totalKmers);
    Debug(Debug::INFO) << "Estimated memory consumption " << totalSizeNeeded/1024/1024 << " MB\n";

    BaseMatrix *subMat;
    if (Parameters::isEqualDbtype(querySeqType, Parameters::DBTYPE_NUCLEOTIDES)) {
        subMat = new NucleotideMatrix(par.seedScoringMatrixFile.nucleotides, 1.0, 0.0);
    } else {
        if (par.alphabetSize == 21) {
            subMat = new SubstitutionMatrix(par.seedScoringMatrixFile.aminoacids, 8.0, -0.2);
        } else {
            SubstitutionMatrix sMat(par.seedScoringMatrixFile.aminoacids, 8.0, -0.2);
            subMat = new ReducedMatrix(sMat.probMatrix, sMat.subMatrixPseudoCounts, sMat.aa2int, sMat.int2aa, sMat.alphabetSize, par.alphabetSize, 8.0);
        }
    }

    // compute splits
    size_t splits = static_cast<size_t>(std::ceil(static_cast<float>(totalSizeNeeded) / memoryLimit));
//    size_t splits = 2;
    if (splits > 1) {
//         security buffer
        splits += 1;
    }
    int outDbType = (Parameters::isEqualDbtype(queryDbr.getDbtype(), Parameters::DBTYPE_NUCLEOTIDES)) ? Parameters::DBTYPE_PREFILTER_REV_RES : Parameters::DBTYPE_PREFILTER_RES;
    Debug(Debug::INFO) << "Process file into " << splits << " parts\n";

    std::vector<std::string> splitFiles;
    for (size_t split = 0; split < splits; split++) {
        tidxdbr.remapData();
        char *entriesData = tidxdbr.getDataUncompressed(tidxdbr.getId(PrefilteringIndexReader::ENTRIES));
        char *entriesOffsetsData = tidxdbr.getDataUncompressed(tidxdbr.getId(PrefilteringIndexReader::ENTRIESOFFSETS));
        int64_t entriesNum = *((int64_t *) tidxdbr.getDataUncompressed(tidxdbr.getId(PrefilteringIndexReader::ENTRIESNUM)));
        int64_t entriesGridSize = *((int64_t *) tidxdbr.getDataUncompressed(tidxdbr.getId(PrefilteringIndexReader::ENTRIESGRIDSIZE)));
        KmerIndex kmerIndex(par.alphabetSize, adjustedKmerSize, entriesData, entriesOffsetsData, entriesNum, entriesGridSize);
//        kmerIndex.printIndex<Parameters::DBTYPE_NUCLEOTIDES>(subMat);
        std::pair<std::string, std::string> tmpFiles;
        if (splits > 1) {
            tmpFiles = Util::createTmpFileNames(par.db3.c_str(), par.db3Index.c_str(), split);
        } else {
            tmpFiles = std::make_pair(par.db3, par.db3Index);
        }
        splitFiles.push_back(tmpFiles.first);
        
        std::string splitFileNameDone = tmpFiles.first + ".done";
        if(FileUtil::fileExists(splitFileNameDone.c_str()) == false) {
            KmerSearch::ExtractKmerAndSortResult sortedKmers = KmerSearch::extractKmerAndSort(totalKmers, split,
                                                                                              splits, queryDbr, par,
                                                                                              subMat,
                                                                                              KMER_SIZE, chooseTopKmer,
                                                                                              par.pickNbest,
                                                                                              isAdjustedKmerLen);
            std::pair<KmerPosition<short> *, size_t> result;
            if (Parameters::isEqualDbtype(queryDbr.getDbtype(), Parameters::DBTYPE_NUCLEOTIDES)) {
                result = KmerSearch::searchInIndex<Parameters::DBTYPE_NUCLEOTIDES>(sortedKmers.kmers,
                                                                                   sortedKmers.kmerCount, kmerIndex);
            } else {
                result = KmerSearch::searchInIndex<Parameters::DBTYPE_AMINO_ACIDS>(sortedKmers.kmers,
                                                                                   sortedKmers.kmerCount, kmerIndex);
            }

            KmerPosition<short> *kmers = result.first;
            size_t kmerCount = result.second;
            if (splits == 1) {
                DBWriter dbw(tmpFiles.first.c_str(), tmpFiles.second.c_str(), 1, par.compressed, outDbType);
                dbw.open();
                if (Parameters::isEqualDbtype(queryDbr.getDbtype(), Parameters::DBTYPE_NUCLEOTIDES)) {
                    KmerSearch::writeResult<Parameters::DBTYPE_NUCLEOTIDES>(dbw, kmers, kmerCount);
                } else {
                    KmerSearch::writeResult<Parameters::DBTYPE_AMINO_ACIDS>(dbw, kmers, kmerCount);
                }
                dbw.close();
            } else {
                if (Parameters::isEqualDbtype(queryDbr.getDbtype(), Parameters::DBTYPE_NUCLEOTIDES)) {
                    writeKmersToDisk<Parameters::DBTYPE_NUCLEOTIDES, KmerEntryRev, short>(tmpFiles.first, kmers,
                            kmerCount );
                } else {
                    writeKmersToDisk<Parameters::DBTYPE_AMINO_ACIDS, KmerEntry, short>(tmpFiles.first, kmers, kmerCount );
                }
            }
            delete[] kmers;
        }
    }
    delete subMat;
    tidxdbr.close();
    queryDbr.close();
    if(splitFiles.size()>1){
        DBWriter writer(par.db3.c_str(), par.db3Index.c_str(), 1, par.compressed, outDbType);
        writer.open(); // 1 GB buffer
        std::vector<char> empty;
        if(Parameters::isEqualDbtype(querySeqType, Parameters::DBTYPE_NUCLEOTIDES)) {
            mergeKmerFilesAndOutput<Parameters::DBTYPE_NUCLEOTIDES, KmerEntryRev>(writer, splitFiles, empty);
        }else{
            mergeKmerFilesAndOutput<Parameters::DBTYPE_AMINO_ACIDS, KmerEntry>(writer, splitFiles, empty);
        }
        for(size_t i = 0; i < splitFiles.size(); i++){
            FileUtil::remove(splitFiles[i].c_str());
            std::string splitFilesDone = splitFiles[i] + ".done";
            FileUtil::remove(splitFilesDone.c_str());
        }
        writer.close();
    }
    return EXIT_SUCCESS;
}
template  <int TYPE>
std::pair<KmerPosition<short> *,size_t > KmerSearch::searchInIndex( KmerPosition<short> *kmers, size_t kmersSize, KmerIndex &kmerIndex) {
    Timer timer;

    kmerIndex.reset();
    KmerIndex::KmerEntry currTargetKmer;
    bool isDone = false;
    if(kmerIndex.hasNextEntry()){
        if(TYPE == Parameters::DBTYPE_NUCLEOTIDES) {
            currTargetKmer = kmerIndex.getNextEntry<Parameters::DBTYPE_NUCLEOTIDES>();
        }else{
            currTargetKmer = kmerIndex.getNextEntry<Parameters::DBTYPE_AMINO_ACIDS>();
        }
    }else{
        isDone = true;
    }

    size_t kmerPos = 0;
    size_t writePos = 0;
    // this is IO bound, optimisation does not make much sense here.
    size_t queryKmer;
    size_t targetKmer;

    while(isDone == false){
        KmerPosition<short> * currQueryKmer = &kmers[kmerPos];
        if(TYPE == Parameters::DBTYPE_NUCLEOTIDES) {
            queryKmer = BIT_SET(currQueryKmer->kmer, 63);
            targetKmer = BIT_SET(currTargetKmer.kmer, 63);
        }else{
            queryKmer = currQueryKmer->kmer;
            targetKmer = currTargetKmer.kmer;
        }

        if(queryKmer < targetKmer){
            while(queryKmer < targetKmer) {
                if (kmerPos + 1 < kmersSize) {
                    kmerPos++;
                } else {
                    isDone = true;
                    break;
                }
                KmerPosition<short> * currQueryKmer = &kmers[kmerPos];
                if(TYPE == Parameters::DBTYPE_NUCLEOTIDES) {
                    queryKmer = BIT_SET(currQueryKmer->kmer, 63);
                }else{
                    queryKmer = currQueryKmer->kmer;
                }
            }
        }else if(targetKmer < queryKmer){
            while(targetKmer < queryKmer){
                if(kmerIndex.hasNextEntry()) {
                    if(TYPE == Parameters::DBTYPE_NUCLEOTIDES) {
                        currTargetKmer = kmerIndex.getNextEntry<Parameters::DBTYPE_NUCLEOTIDES>();
                    }else{
                        currTargetKmer = kmerIndex.getNextEntry<Parameters::DBTYPE_AMINO_ACIDS>();
                    }
                    if(TYPE == Parameters::DBTYPE_NUCLEOTIDES) {
                        targetKmer = BIT_SET(currTargetKmer.kmer, 63);
                    }else{
                        targetKmer = currTargetKmer.kmer;
                    }
                    //TODO remap logic to speed things up
                }else{
                    isDone = true;
                    break;
                }
            }
        }else{
            if(TYPE == Parameters::DBTYPE_NUCLEOTIDES){
                //  00 No problem here both are forward
                //  01 We can revert the query of target, lets invert the query.
                //  10 Same here, we can revert query to match the not inverted target
                //  11 Both are reverted so no problem!
                //  So we need just 1 bit of information to encode all four states
                bool targetIsReverse = (BIT_CHECK(currQueryKmer->kmer, 63) == false);
                bool repIsReverse = (BIT_CHECK(currTargetKmer.kmer, 63) == false);
                bool queryNeedsToBeRev = false;
                // we now need 2 byte of information (00),(01),(10),(11)
                // we need to flip the coordinates of the query
                short queryPos=0;
                short targetPos=0;
                // revert kmer in query hits normal kmer in target
                // we need revert the query
                if (repIsReverse == true && targetIsReverse == false){
                    queryPos = currTargetKmer.pos;
                    targetPos = currQueryKmer->pos;
                    queryNeedsToBeRev = true;
                    // both k-mers were extracted on the reverse strand
                    // this is equal to both are extract on the forward strand
                    // we just need to offset the position to the forward strand
                }else if (repIsReverse == true && targetIsReverse == true){
                    queryPos = (currTargetKmer.seqLen - 1) - currTargetKmer.pos;
                    targetPos = (currQueryKmer->seqLen - 1) - currQueryKmer->pos;
                    queryNeedsToBeRev = false;
                    // query is not revers but target k-mer is reverse
                    // instead of reverting the target, we revert the query and offset the the query/target position
                }else if (repIsReverse == false && targetIsReverse == true){
                    queryPos = (currTargetKmer.seqLen - 1) - currTargetKmer.pos;
                    targetPos = (currQueryKmer->seqLen - 1) - currQueryKmer->pos;
                    queryNeedsToBeRev = true;
                    // both are forward, everything is good here
                }else{
                    queryPos = currTargetKmer.pos;
                    targetPos =  currQueryKmer->pos;
                    queryNeedsToBeRev = false;
                }
                (kmers+writePos)->pos = queryPos - targetPos;
                size_t id = (queryNeedsToBeRev) ? BIT_CLEAR(static_cast<size_t >(currTargetKmer.id), 63) : BIT_SET(static_cast<size_t >(currTargetKmer.id), 63);
                (kmers+writePos)->kmer = id;
            }else{
                // i - j
                (kmers+writePos)->kmer= currTargetKmer.id;
//                std::cout << currTargetKmer.pos - currQueryKmer->pos << "\t" << currTargetKmer.pos << "\t" << currQueryKmer->pos << std::endl;
                (kmers+writePos)->pos = currTargetKmer.pos - currQueryKmer->pos;
            }
            (kmers+writePos)->id = currQueryKmer->id;
            (kmers+writePos)->seqLen = currQueryKmer->seqLen;

            writePos++;
            if(kmerPos+1<kmersSize){
                kmerPos++;
            }
        }
    }
    Debug(Debug::INFO) << "Time to find k-mers: " << timer.lap() << "\n";
    timer.reset();
    if(TYPE == Parameters::DBTYPE_NUCLEOTIDES) {
        omptl::sort(kmers, kmers + writePos, KmerPosition<short>::compareRepSequenceAndIdAndDiagReverse);
    }else{
        omptl::sort(kmers, kmers + writePos, KmerPosition<short>::compareRepSequenceAndIdAndDiag);
    }

    Debug(Debug::INFO) << "Time to sort: " << timer.lap() << "\n";
    return std::make_pair(kmers, writePos);
}

template std::pair<KmerPosition<short> *,size_t > KmerSearch::searchInIndex<0>( KmerPosition<short> *kmers, size_t kmersSize, KmerIndex &kmerIndex);
template std::pair<KmerPosition<short> *,size_t > KmerSearch::searchInIndex<1>( KmerPosition<short> *kmers, size_t kmersSize, KmerIndex &kmerIndex);

#undef SIZE_T_MAX
