#this file is located at gem5/src/cpu/pred
#replace this file with the following code (I deleted the original copyright comments for cleaner view) 
#*or* add 'OGEHLBP' at the end of SimObject, and add Source('ogehl.cc') if you want to keep the original file. 

Import('*')


SimObject('BranchPredictor.py',
    sim_objects=[
    'BranchPredictor',
    'ConditionalPredictor',
    'IndirectPredictor', 'SimpleIndirectPredictor',
    'BranchTargetBuffer', 'SimpleBTB', 'BTBIndexingPolicy', 'BTBSetAssociative',
    'ReturnAddrStack',
    'LocalBP', 'TournamentBP', 'BiModeBP', 'TAGEBase', 'TAGE', 'LoopPredictor',
    'TAGE_SC_L_TAGE', 'TAGE_SC_L_TAGE_64KB', 'TAGE_SC_L_TAGE_8KB',
    'LTAGE', 'TAGE_SC_L_LoopPredictor', 'StatisticalCorrector', 'TAGE_SC_L',
    'TAGE_SC_L_64KB_StatisticalCorrector',
    'TAGE_SC_L_8KB_StatisticalCorrector',
    'TAGE_SC_L_64KB', 'TAGE_SC_L_8KB', 'MultiperspectivePerceptron',
    'MultiperspectivePerceptron8KB', 'MultiperspectivePerceptron64KB',
    'MPP_TAGE', 'MPP_LoopPredictor', 'MPP_StatisticalCorrector',
    'MultiperspectivePerceptronTAGE', 'MPP_StatisticalCorrector_64KB',
    'MultiperspectivePerceptronTAGE64KB', 'MPP_TAGE_8KB',
    'MPP_LoopPredictor_8KB', 'MPP_StatisticalCorrector_8KB',
    'MultiperspectivePerceptronTAGE8KB', 'GshareBP', 'GEHLBP','OGEHLBP'],
    enums=['BranchType', 'TargetProvider'])

Source('bpred_unit.cc')
Source('2bit_local.cc')
Source('simple_indirect.cc')
Source('conditional.cc')
Source('indirect.cc')
Source('ras.cc')
Source('tournament.cc')
Source('bi_mode.cc')
Source('tage_base.cc')
Source('tage.cc')
Source('loop_predictor.cc')
Source('ltage.cc')
Source('multiperspective_perceptron.cc')
Source('multiperspective_perceptron_8KB.cc')
Source('multiperspective_perceptron_64KB.cc')
Source('multiperspective_perceptron_tage.cc')
Source('multiperspective_perceptron_tage_8KB.cc')
Source('multiperspective_perceptron_tage_64KB.cc')
Source('statistical_corrector.cc')
Source('tage_sc_l.cc')
Source('tage_sc_l_8KB.cc')
Source('tage_sc_l_64KB.cc')
Source('gshare.cc')
Source('gehl.cc')
Source('ogehl.cc')
Source('btb.cc')
Source('simple_btb.cc')
DebugFlag('Indirect')
DebugFlag('BTB')
DebugFlag('RAS')
DebugFlag('FreeList')
DebugFlag('Branch')
DebugFlag('Tage')
DebugFlag('LTage')
DebugFlag('TageSCL')
