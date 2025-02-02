#include <cassert>

#include "FileUtil.h"
#include "CommandCaller.h"
#include "Util.h"
#include "Debug.h"
#include "Parameters.h"

namespace linclust {
#include "easycluster.sh.h"
}

void setEasyLinclustDefaults(Parameters *p) {
    p->spacedKmer = true;
    p->removeTmpFiles = true;
    p->covThr = 0.8;
    p->evalThr = 0.001;
    p->createdbMode = Parameters::SEQUENCE_SPLIT_MODE_SOFT;
    //p->alignmentMode = Parameters::ALIGNMENT_MODE_SCORE_COV_SEQID;
    p->orfStartMode = 1;
    p->orfMinLength = 10;
    p->orfMaxLength = 32734;
    p->evalProfile = 0.1;
}
void setEasyLinclustMustPassAlong(Parameters *p) {
    p->PARAM_SPACED_KMER_MODE.wasSet = true;
    p->PARAM_REMOVE_TMP_FILES.wasSet = true;
    p->PARAM_C.wasSet = true;
    p->PARAM_E.wasSet = true;
    //p->PARAM_ALIGNMENT_MODE.wasSet = true;
    p->PARAM_ORF_START_MODE.wasSet = true;
    p->PARAM_ORF_MIN_LENGTH.wasSet = true;
    p->PARAM_ORF_MAX_LENGTH.wasSet = true;
    p->PARAM_E_PROFILE.wasSet = true;
}

int easylinclust(int argc, const char **argv, const Command &command) {
    Parameters &par = Parameters::getInstance();
    par.overrideParameterDescription((Command &)command, par.PARAM_ADD_BACKTRACE.uniqid, NULL, NULL, par.PARAM_ADD_BACKTRACE.category | MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &)command, par.PARAM_ALT_ALIGNMENT.uniqid, NULL, NULL, par.PARAM_ALT_ALIGNMENT.category | MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &)command, par.PARAM_RESCORE_MODE.uniqid, NULL, NULL, par.PARAM_RESCORE_MODE.category | MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &)command, par.PARAM_MAX_REJECTED.uniqid, NULL, NULL, par.PARAM_MAX_REJECTED.category | MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &)command, par.PARAM_MAX_ACCEPT.uniqid, NULL, NULL, par.PARAM_MAX_ACCEPT.category | MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &)command, par.PARAM_S.uniqid, "Sensitivity will be automatically determined but can be adjusted", NULL, par.PARAM_S.category | MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &)command, par.PARAM_INCLUDE_ONLY_EXTENDABLE.uniqid, NULL, NULL, par.PARAM_INCLUDE_ONLY_EXTENDABLE.category | MMseqsParameter::COMMAND_EXPERT);
    for (size_t i = 0; i < par.createdb.size(); i++){
        par.overrideParameterDescription((Command &)command, par.createdb[i]->uniqid, NULL, NULL, par.createdb[i]->category | MMseqsParameter::COMMAND_EXPERT);
    }
    par.overrideParameterDescription((Command &) command, par.PARAM_THREADS.uniqid, NULL, NULL, par.PARAM_THREADS.category & ~MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &) command, par.PARAM_V.uniqid, NULL, NULL, par.PARAM_V.category & ~MMseqsParameter::COMMAND_EXPERT);

    setEasyLinclustDefaults(&par);
    par.parseParameters(argc, argv, command, true, Parameters::PARSE_VARIADIC, 0);
    setEasyLinclustMustPassAlong(&par);

    std::string tmpDir = par.filenames.back();
    std::string hash = SSTR(par.hashParameter(par.filenames, *command.params));
    if (par.reuseLatest) {
        hash = FileUtil::getHashFromSymLink(tmpDir + "/latest");
    }
    tmpDir = FileUtil::createTemporaryDirectory(tmpDir, hash);
    par.filenames.pop_back();

    CommandCaller cmd;
    cmd.addVariable("TMP_PATH", tmpDir.c_str());
    cmd.addVariable("RESULTS", par.filenames.back().c_str());
    par.filenames.pop_back();
    cmd.addVariable("REMOVE_TMP", par.removeTmpFiles ? "TRUE" : NULL);

    cmd.addVariable("RUNNER", par.runner.c_str());
    cmd.addVariable("CREATEDB_PAR", par.createParameterString(par.createdb).c_str());
    cmd.addVariable("CLUSTER_PAR", par.createParameterString(par.linclustworkflow, true).c_str());
    cmd.addVariable("CLUSTER_MODULE", "linclust");
    cmd.addVariable("THREADS_PAR", par.createParameterString(par.onlythreads).c_str());
    cmd.addVariable("VERBOSITY_PAR", par.createParameterString(par.onlyverbosity).c_str());

    std::string program = tmpDir + "/easycluster.sh";
    FileUtil::writeFile(program, linclust::easycluster_sh, linclust::easycluster_sh_len);
    cmd.execProgram(program.c_str(), par.filenames);

    // Should never get here
    assert(false);
    return 0;
}
