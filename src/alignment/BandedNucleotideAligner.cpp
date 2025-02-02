//
// Written by Martin Steinegger
//
// Wrapper for KSW2 aligner.
// Local banded nucleotide aligner
//
#include "Parameters.h"
#include "DistanceCalculator.h"
#include "ksw2.h"
#include "BandedNucleotideAligner.h"

#include "Util.h"
#include "SubstitutionMatrix.h"
#include "Debug.h"
#include "StripedSmithWaterman.h"


BandedNucleotideAligner::BandedNucleotideAligner(BaseMatrix * subMat, size_t maxSequenceLength, int gapo, int gape) :
fastMatrix(SubstitutionMatrix::createAsciiSubMat(*subMat))
{

    targetSeq =  new uint8_t[maxSequenceLength + 1];
    targetSeqRev =  new uint8_t[maxSequenceLength + 1];
    querySeq =  new uint8_t[maxSequenceLength + 1];
    querySeqRev =  new uint8_t[maxSequenceLength + 1];
    queryRevCompSeq =  new uint8_t[maxSequenceLength + 1];
    queryRevCompSeqRev =  new uint8_t[maxSequenceLength + 1];
    queryRevCompCharSeq  =  new char[maxSequenceLength + 1];
    mat = new int8_t[subMat->alphabetSize*subMat->alphabetSize];
    this->subMat = (NucleotideMatrix*) subMat;
    for (int i = 0; i < subMat->alphabetSize; i++) {
        for (int j = 0; j < subMat->alphabetSize; j++) {
            mat[i*subMat->alphabetSize + j] = subMat->subMatrix[i][j];
        }
    }
    this->gape = gape;
    this->gapo = gapo;
}

BandedNucleotideAligner::~BandedNucleotideAligner(){
    delete [] querySeq;
    delete [] targetSeq;
    delete [] targetSeqRev;
    delete [] querySeqRev;
    delete [] queryRevCompSeq;
    delete [] queryRevCompSeqRev;
    delete [] queryRevCompCharSeq;
    delete [] fastMatrix.matrixData;
    delete [] fastMatrix.matrix;
    delete [] mat;
}

void BandedNucleotideAligner::initQuery(Sequence * query){
    querySeqObj = query;
    for (int i = 0; i < query->L; ++i) {
        querySeq[i] = query->int_sequence[i];
    }
    SmithWaterman::seq_reverse((int8_t *)querySeqRev, (int8_t *)querySeq, query->L);
    // needed for rev. complement
    for (int pos = query->L - 1; pos > -1; pos--) {
        int res = query->int_sequence[pos];
        queryRevCompSeq[(query->L - 1) - pos] = subMat->reverseResidue(res);
        queryRevCompCharSeq[(query->L - 1) - pos] = subMat->int2aa[subMat->reverseResidue(res)];

    }
    SmithWaterman::seq_reverse((int8_t *)queryRevCompSeqRev, (int8_t *)queryRevCompSeq, query->L);
}



s_align BandedNucleotideAligner::align(Sequence * targetSeqObj,
                                       int diagonal, bool reverse,
                                       std::string & backtrace, int & aaIds,
                                       EvalueComputation * evaluer, bool wrappedScoring)
{
    char * queryCharSeqAlign = (char*) querySeqObj->getSeqData();
    uint8_t * querySeqRevAlign = querySeqRev;
    uint8_t * querySeqAlign = querySeq;
    if(reverse){
        queryCharSeqAlign = queryRevCompCharSeq;
        querySeqRevAlign  = queryRevCompSeqRev;
        querySeqAlign     = queryRevCompSeq;
    }

    for (int i = 0; i < targetSeqObj->L; ++i) {
        targetSeq[i] = targetSeqObj->int_sequence[i];
    }
    SmithWaterman::seq_reverse((int8_t *)targetSeqRev, (int8_t *)targetSeq, targetSeqObj->L);

    int qUngappedStartPos, qUngappedEndPos, dbUngappedStartPos, dbUngappedEndPos;

    DistanceCalculator::LocalAlignment alignment;
    int queryLen = querySeqObj->L;
    int origQueryLen = queryLen;
    if (wrappedScoring) {
        alignment = DistanceCalculator::computeUngappedWrappedAlignment(
                queryCharSeqAlign, querySeqObj->L, targetSeqObj->getSeqData(), targetSeqObj->L,
                diagonal, fastMatrix.matrix, Parameters::RESCORE_MODE_ALIGNMENT);
        origQueryLen = queryLen/2;
    }
    else {
        alignment = DistanceCalculator::computeUngappedAlignment(
                queryCharSeqAlign, querySeqObj->L, targetSeqObj->getSeqData(), targetSeqObj->L,
                diagonal, fastMatrix.matrix, Parameters::RESCORE_MODE_ALIGNMENT);
    }


    unsigned int distanceToDiagonal = alignment.distToDiagonal;
    diagonal = alignment.diagonal;

    if (diagonal >= 0){
        qUngappedStartPos = alignment.startPos + distanceToDiagonal;
        qUngappedEndPos = alignment.endPos + distanceToDiagonal;
        dbUngappedStartPos = alignment.startPos;
        dbUngappedEndPos = alignment.endPos;
    }else {
        qUngappedStartPos = alignment.startPos;
        qUngappedEndPos = alignment.endPos;
        dbUngappedStartPos = alignment.startPos + distanceToDiagonal;
        dbUngappedEndPos = alignment.endPos + distanceToDiagonal;
    }
    if(qUngappedEndPos-qUngappedStartPos == origQueryLen - 1
       && dbUngappedStartPos == 0 && dbUngappedEndPos == targetSeqObj->L - 1){
        s_align result;
        uint32_t * retCigar = new uint32_t[1];
        retCigar[0] = 0;
        retCigar[0] = origQueryLen << 4;
        result.cigar = retCigar;
        result.cigarLen = 1;
        result.score1 = alignment.score;
        result.qStartPos1 = qUngappedStartPos;
        result.qEndPos1 = qUngappedEndPos;
        result.dbEndPos1 = dbUngappedEndPos;
        result.dbStartPos1 = dbUngappedStartPos;
        result.qCov = SmithWaterman::computeCov(result.qStartPos1, result.qEndPos1, querySeqObj->L);
        if(wrappedScoring)
            result.qCov = std::min(1.0f, result.qCov*2);
        result.tCov = SmithWaterman::computeCov(result.dbStartPos1, result.dbEndPos1, targetSeqObj->L);
        result.evalue = evaluer->computeEvalue(result.score1, origQueryLen);
        for (int i = qUngappedStartPos; i <= qUngappedEndPos; i++) {
            aaIds += (querySeqAlign[i] == targetSeq[dbUngappedStartPos + (i - dbUngappedStartPos)]) ? 1 : 0;
        }
        for(int pos = 0; pos <  origQueryLen; pos++){
            backtrace.append("M");
        }
        return result;
    }
//    printf("%d\t%d\t%d\n", alignment.score,  alignment.startPos, alignment.endPos);

    // get middle position of ungapped alignment
    int qStartRev = (querySeqObj->L  - qUngappedEndPos) - 1;
    int tStartRev = (targetSeqObj->L - dbUngappedEndPos) - 1;

    ksw_extz_t ez;
    int flag = 0;
    flag |= KSW_EZ_SCORE_ONLY;
    flag |= KSW_EZ_EXTZ_ONLY;

    int queryRevLenToAlign = querySeqObj->L - qStartRev;
    if (wrappedScoring && queryRevLenToAlign > origQueryLen)
        queryRevLenToAlign = origQueryLen;

    ksw_extz2_sse(0, queryRevLenToAlign, querySeqRevAlign + qStartRev, targetSeqObj->L - tStartRev, targetSeqRev + tStartRev, 5, mat, gapo, gape, 64, 40, flag, &ez);

    int qStartPos = querySeqObj->L  - ( qStartRev + ez.max_q ) -1;
    int tStartPos = targetSeqObj->L - ( tStartRev + ez.max_t ) -1;

    int alignFlag = 0;
    alignFlag |= KSW_EZ_EXTZ_ONLY;

    ksw_extz_t ezAlign;
//    ezAlign.cigar = cigar;
//    printf("%d %d\n", qStartPos, tStartPos);
    memset(&ezAlign, 0, sizeof(ksw_extz_t));

    int queryLenToAlign = querySeqObj->L-qStartPos;
    if (wrappedScoring && queryLenToAlign > origQueryLen)
        queryLenToAlign = origQueryLen;
    ksw_extz2_sse(0, queryLenToAlign, querySeqAlign+qStartPos, targetSeqObj->L-tStartPos, targetSeq+tStartPos, 5,
                  mat, gapo, gape, 64, 40, alignFlag, &ezAlign);

    std::string letterCode = "MID";
    uint32_t * retCigar;

    if (ez.max_q > ezAlign.max_q && ez.max_t > ezAlign.max_t){

        ksw_extz2_sse(0, queryRevLenToAlign, querySeqRevAlign + qStartRev, targetSeqObj->L - tStartRev,
                      targetSeqRev + tStartRev, 5, mat, gapo, gape, 64, 40, alignFlag, &ezAlign);

        retCigar = new uint32_t[ezAlign.n_cigar];
        for(int i = 0; i < ezAlign.n_cigar; i++){
            retCigar[i]=ezAlign.cigar[ezAlign.n_cigar-1-i];
        }
    }
    else {
        retCigar = new uint32_t[ezAlign.n_cigar];
        for(int i = 0; i < ezAlign.n_cigar; i++){
            retCigar[i]=ezAlign.cigar[i];
        }
   }

    s_align result;
    result.cigar = retCigar;
    result.cigarLen = ezAlign.n_cigar;
    result.score1 = ezAlign.max;
    result.qStartPos1 = qStartPos;
    result.qEndPos1 = qStartPos+ezAlign.max_q;
    result.dbEndPos1 = tStartPos+ezAlign.max_t;
    result.dbStartPos1 = tStartPos;
    result.qCov = SmithWaterman::computeCov(result.qStartPos1, result.qEndPos1, querySeqObj->L);
    if(wrappedScoring)
        result.qCov = std::min(1.0f, result.qCov*2);
    result.tCov = SmithWaterman::computeCov(result.dbStartPos1, result.dbEndPos1, targetSeqObj->L);
    result.evalue = evaluer->computeEvalue(result.score1, origQueryLen);
    if(result.cigar){
        int32_t targetPos = result.dbStartPos1, queryPos = result.qStartPos1;
        for (int32_t c = 0; c < result.cigarLen; ++c) {
            char letter = SmithWaterman::cigar_int_to_op(result.cigar[c]);
            uint32_t length = SmithWaterman::cigar_int_to_len(result.cigar[c]);
            backtrace.reserve(length);

            for (uint32_t i = 0; i < length; ++i){
                if (letter == 'M') {
                    if (targetSeq[targetPos] == querySeqAlign[queryPos]){
                        aaIds++;
                    }
                    ++queryPos;
                    ++targetPos;
                    backtrace.append("M");
                } else {
                    if (letter == 'I') {
                        ++queryPos;
                        backtrace.append("I");
                    }
                    else{
                        ++targetPos;
                        backtrace.append("D");
                    }
                }
            }
        }
    }
    
    free(ezAlign.cigar);
    return result;
//        std::cout << static_cast<float>(aaIds)/ static_cast<float>(alignment.len) << std::endl;

}
