#include "IndexBuilder.h"
#include "tantan.h"

#ifdef OPENMP
#include <omp.h>
#endif

char* getScoreLookup(BaseMatrix &matrix) {
    char *idScoreLookup = NULL;
    idScoreLookup = new char[matrix.alphabetSize];
    for (int aa = 0; aa < matrix.alphabetSize; aa++){
        short score = matrix.subMatrix[aa][aa];
        if (score > SCHAR_MAX || score < SCHAR_MIN) {
            Debug(Debug::WARNING) << "Truncating substitution matrix diagonal score!";
        }
        idScoreLookup[aa] = (char) score;
    }
    return idScoreLookup;
}


class DbInfo {
public:
    DbInfo(size_t dbFrom, size_t dbTo, unsigned int effectiveKmerSize, DBReader<unsigned int> & reader) {
        tableSize = 0;
        aaDbSize = 0;
        size_t dbSize = dbTo - dbFrom;
        sequenceOffsets = new size_t[dbSize];
        sequenceOffsets[0] = 0;
        for (size_t id = dbFrom; id < dbTo; id++) {
            const int seqLen = reader.getSeqLen(id);
            aaDbSize += seqLen;
            size_t idFromNull = (id - dbFrom);
            if (id < dbTo - 1) {
                sequenceOffsets[idFromNull + 1] = sequenceOffsets[idFromNull] + seqLen;
            }
            if (Util::overlappingKmers(seqLen, effectiveKmerSize > 0)) {
                tableSize += 1;
            }
        }
    }

    ~DbInfo() {
        delete[] sequenceOffsets;
    }

    size_t tableSize;
    size_t aaDbSize;
    size_t *sequenceOffsets;
};


void IndexBuilder::fillDatabase(IndexTable *indexTable, SequenceLookup **maskedLookup,
                                SequenceLookup **unmaskedLookup,BaseMatrix &subMat, Sequence *seq,
                                DBReader<unsigned int> *dbr, size_t dbFrom, size_t dbTo, int kmerThr,
                                bool mask, bool maskLowerCaseMode) {
    Debug(Debug::INFO) << "Index table: counting k-mers\n";

    const bool isProfile = Parameters::isEqualDbtype(seq->getSeqType(), Parameters::DBTYPE_HMM_PROFILE);

    dbTo = std::min(dbTo, dbr->getSize());
    size_t dbSize = dbTo - dbFrom;
    DbInfo* info = new DbInfo(dbFrom, dbTo, seq->getEffectiveKmerSize(), *dbr);

    SequenceLookup *sequenceLookup;
    if (unmaskedLookup != NULL && maskedLookup == NULL) {
        *unmaskedLookup = new SequenceLookup(dbSize, info->aaDbSize);
        sequenceLookup = *unmaskedLookup;
    } else if (unmaskedLookup == NULL && maskedLookup != NULL) {
        *maskedLookup = new SequenceLookup(dbSize, info->aaDbSize);
        sequenceLookup = *maskedLookup;
    } else if (unmaskedLookup != NULL && maskedLookup != NULL) {
        *unmaskedLookup = new SequenceLookup(dbSize, info->aaDbSize);
        *maskedLookup = new SequenceLookup(dbSize, info->aaDbSize);
        sequenceLookup = *maskedLookup;
    } else{
        Debug(Debug::ERROR) << "This should not happen\n";
        EXIT(EXIT_FAILURE);
    }

    // need to prune low scoring k-mers through masking
    ProbabilityMatrix *probMatrix = NULL;
    if (maskedLookup != NULL) {
        probMatrix = new ProbabilityMatrix(subMat);
    }

    // identical scores for memory reduction code
    char *idScoreLookup = NULL;
    if (Parameters::isEqualDbtype(seq->getSeqType(), Parameters::DBTYPE_PROFILE_STATE_SEQ) == false) {
        idScoreLookup = getScoreLookup(subMat);
    }
    Debug::Progress progress(dbTo-dbFrom);

    size_t maskedResidues = 0;
    size_t totalKmerCount = 0;
    #pragma omp parallel
    {
        unsigned int thread_idx = 0;
#ifdef OPENMP
        thread_idx = static_cast<unsigned int>(omp_get_thread_num());
#endif

        Indexer idxer(static_cast<unsigned int>(indexTable->getAlphabetSize()), seq->getKmerSize());
        Sequence s(seq->getMaxLen(), seq->getSeqType(), &subMat, seq->getKmerSize(), seq->isSpaced(), false, true, seq->getSpacedKmerPattern());

        KmerGenerator *generator = NULL;
        if (isProfile) {
            generator = new KmerGenerator(seq->getKmerSize(), indexTable->getAlphabetSize(), kmerThr);
            generator->setDivideStrategy(s.profile_matrix);
        }

        unsigned int *buffer = new unsigned int[seq->getMaxLen()];
        char *charSequence = new char[seq->getMaxLen()];

        #pragma omp for schedule(dynamic, 100) reduction(+:totalKmerCount, maskedResidues)
        for (size_t id = dbFrom; id < dbTo; id++) {
            progress.updateProgress();

            s.resetCurrPos();
            char *seqData = dbr->getData(id, thread_idx);
            unsigned int qKey = dbr->getDbKey(id);

            s.mapSequence(id - dbFrom, qKey, seqData, dbr->getSeqLen(id));

            // count similar or exact k-mers based on sequence type
            if (isProfile) {
                // Find out if we should also mask profiles
                totalKmerCount += indexTable->addSimilarKmerCount(&s, generator);
                (*unmaskedLookup)->addSequence(s.int_consensus_sequence, s.L, id - dbFrom, info->sequenceOffsets[id - dbFrom]);
            } else {
                // Do not mask if column state sequences are used
                if (unmaskedLookup != NULL) {
                    (*unmaskedLookup)->addSequence(s.int_sequence, s.L, id - dbFrom, info->sequenceOffsets[id - dbFrom]);
                }
                if (mask == true) {
                    for (int i = 0; i < s.L; ++i) {
                        charSequence[i] = (char) s.int_sequence[i];
                    }
                    // s.print();
                    maskedResidues += tantan::maskSequences(charSequence,
                                                            charSequence + s.L,
                                                            50 /*options.maxCycleLength*/,
                                                            probMatrix->probMatrixPointers,
                                                            0.005 /*options.repeatProb*/,
                                                            0.05 /*options.repeatEndProb*/,
                                                            0.9 /*options.repeatOffsetProbDecay*/,
                                                            0, 0,
                                                            0.9 /*options.minMaskProb*/,
                                                            probMatrix->hardMaskTable);

                    for (int i = 0; i < s.L; i++) {
                        s.int_sequence[i] = charSequence[i];
                    }
                }

                if(maskLowerCaseMode == true && (Parameters::isEqualDbtype(s.getSequenceType(), Parameters::DBTYPE_AMINO_ACIDS) ||
                                                  Parameters::isEqualDbtype(s.getSequenceType(), Parameters::DBTYPE_NUCLEOTIDES))) {
                    const char * charSeq = s.getSeqData();
                    int maskLetter = subMat.aa2int[(int)'X'];
                    for (int i = 0; i < s.L; i++) {
                        bool isLowerCase = (islower(charSeq[i]));
                        maskedResidues += isLowerCase;
                        s.int_sequence[i] = isLowerCase ? maskLetter : s.int_sequence[i];
                    }
                }
                if(maskedLookup != NULL){
                    (*maskedLookup)->addSequence(s.int_sequence, s.L, id - dbFrom, info->sequenceOffsets[id - dbFrom]);
                }

                totalKmerCount += indexTable->addKmerCount(&s, &idxer, buffer, kmerThr, idScoreLookup);
            }
        }

        delete[] charSequence;
        delete[] buffer;

        if (generator != NULL) {
            delete generator;
        }
    }

    if(probMatrix != NULL) {
        delete probMatrix;
    }

    Debug(Debug::INFO) << "Index table: Masked residues: " << maskedResidues << "\n";
    if(totalKmerCount == 0) {
        Debug(Debug::ERROR) << "No k-mer could be extracted for the database " << dbr->getDataFileName() << ".\n"
                            << "Maybe the sequences length is less than 14 residues.\n";
        if (maskedResidues == true){
            Debug(Debug::ERROR) << " or contains only low complexity regions.";
            Debug(Debug::ERROR) << "Use --mask 0 to deactivate the low complexity filter.\n";
        }
        EXIT(EXIT_FAILURE);
    }

    dbr->remapData();

    //TODO find smart way to remove extrem k-mers without harming huge protein families
//    size_t lowSelectiveResidues = 0;
//    const float dbSize = static_cast<float>(dbTo - dbFrom);
//    for(size_t kmerIdx = 0; kmerIdx < indexTable->getTableSize(); kmerIdx++){
//        size_t res = (size_t) indexTable->getOffset(kmerIdx);
//        float selectivityOfKmer = (static_cast<float>(res)/dbSize);
//        if(selectivityOfKmer > 0.005){
//            indexTable->getOffset()[kmerIdx] = 0;
//            lowSelectiveResidues += res;
//        }
//    }
//    Debug(Debug::INFO) << "Index table: Remove "<< lowSelectiveResidues <<" none selective residues\n";
//    Debug(Debug::INFO) << "Index table: init... from "<< dbFrom << " to "<< dbTo << "\n";

    indexTable->initMemory(info->tableSize);
    indexTable->init();

    delete info;
    Debug::Progress progress2(dbTo-dbFrom);

    Debug(Debug::INFO) << "Index table: fill\n";
    #pragma omp parallel
    {
        unsigned int thread_idx = 0;
#ifdef OPENMP
        thread_idx = static_cast<unsigned int>(omp_get_thread_num());
#endif
        Sequence s(seq->getMaxLen(), seq->getSeqType(), &subMat, seq->getKmerSize(), seq->isSpaced(), false, true, seq->getSpacedKmerPattern());
        Indexer idxer(static_cast<unsigned int>(indexTable->getAlphabetSize()), seq->getKmerSize());
        IndexEntryLocalTmp *buffer = new IndexEntryLocalTmp[seq->getMaxLen()];

        KmerGenerator *generator = NULL;
        if (isProfile) {
            generator = new KmerGenerator(seq->getKmerSize(), indexTable->getAlphabetSize(), kmerThr);
            generator->setDivideStrategy(s.profile_matrix);
        }

        #pragma omp for schedule(dynamic, 100)
        for (size_t id = dbFrom; id < dbTo; id++) {
            s.resetCurrPos();
            progress2.updateProgress();

            unsigned int qKey = dbr->getDbKey(id);
            if (isProfile) {
                s.mapSequence(id - dbFrom, qKey, dbr->getData(id, thread_idx), dbr->getSeqLen(id));
                indexTable->addSimilarSequence(&s, generator, &idxer);
            } else {
                s.mapSequence(id - dbFrom, qKey, sequenceLookup->getSequence(id - dbFrom));
                indexTable->addSequence(&s, &idxer, buffer, kmerThr, idScoreLookup);
            }
        }

        if (generator != NULL) {
            delete generator;
        }

        delete [] buffer;
    }
    if(idScoreLookup!=NULL){
        delete[] idScoreLookup;
    }
    indexTable->revertPointer();
    indexTable->sortDBSeqLists();
}
