// ctc/chain-den-graph.cc

// Copyright      2015   Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.


#include "chain/chain-den-graph.h"
#include "hmm/hmm-utils.h"

namespace kaldi {
namespace chain {


DenominatorGraph::DenominatorGraph(const fst::StdVectorFst &fst,
                                   int32 num_pdfs) {
  SetTransitions(fst, num_pdfs);
  SetInitialProbs(fst);
}

const Int32Pair* DenominatorGraph::BackwardTransitions() const {
  return backward_transitions_.Data();
}

const Int32Pair* DenominatorGraph::ForwardTransitions() const {
  return forward_transitions_.Data();
}

const DenominatorGraphTransition* DenominatorGraph::Transitions() const {
  return transitions_.Data();
}

const CuVector<BaseFloat>& DenominatorGraph::InitialProbs() const {
  return initial_probs_;
}

void DenominatorGraph::SetTransitions(const fst::StdVectorFst &fst,
                                      int32 num_pdfs) {
  int32 num_states = fst.NumStates();

  std::vector<std::vector<DenominatorGraphTransition> >
      transitions_out(num_states),
      transitions_in(num_states);
  for (int32 s = 0; s < num_states; s++) {
    for (fst::ArcIterator<fst::StdVectorFst> aiter(fst, s); !aiter.Done();
         aiter.Next()) {
      const fst::StdArc &arc = aiter.Value();
      DenominatorGraphTransition transition;
      transition.transition_prob = exp(-arc.weight.Value());
      transition.pdf_id = arc.ilabel - 1;
      transition.hmm_state = arc.nextstate;
      KALDI_ASSERT(transition.pdf_id >= 0 && transition.pdf_id < num_pdfs);
      transitions_out[s].push_back(transition);
      // now the reverse transition.
      transition.hmm_state = s;
      transitions_in[arc.nextstate].push_back(transition);
    }
  }

  std::vector<Int32Pair> forward_transitions(num_states);
  std::vector<Int32Pair> backward_transitions(num_states);
  std::vector<DenominatorGraphTransition> transitions;

  for (int32 s = 0; s < num_states; s++) {
    forward_transitions[s].first = static_cast<int32>(transitions.size());
    transitions.insert(transitions.end(), transitions_out[s].begin(),
                       transitions_out[s].end());
    forward_transitions[s].second = static_cast<int32>(transitions.size());
  }
  for (int32 s = 0; s < num_states; s++) {
    backward_transitions[s].first = static_cast<int32>(transitions.size());
    transitions.insert(transitions.end(), transitions_in[s].begin(),
                       transitions_in[s].end());
    backward_transitions[s].second = static_cast<int32>(transitions.size());
  }

  forward_transitions_ = forward_transitions;
  backward_transitions_ = backward_transitions;
  transitions_ = transitions;
}

void DenominatorGraph::SetInitialProbs(const fst::StdVectorFst &fst) {
  // we set only the start-state to have probability mass, and then 100
  // iterations of HMM propagation, over which we average the probabilities.
  // initial probs won't end up making a huge difference as we won't be using
  // derivatives from the first few frames, so this isn't 100% critical.
  int32 num_iters = 100;
  int32 num_states = fst.NumStates();

  // we normalize each state so that it sums to one (including
  // final-probs)... this is needed because the 'chain' code doesn't
  // have transition probabilities.
  Vector<double> normalizing_factor(num_states);
  for (int32 s = 0; s < num_states; s++) {
    double tot_prob = exp(-fst.Final(s).Value());
    for (fst::ArcIterator<fst::StdVectorFst> aiter(fst, s); !aiter.Done();
         aiter.Next()) {
      tot_prob += exp(-aiter.Value().weight.Value());
    }
    KALDI_ASSERT(tot_prob > 0.0 && tot_prob < 100.0);
    normalizing_factor(s) = 1.0 / tot_prob;
  }

  Vector<double> cur_prob(num_states), next_prob(num_states),
      avg_prob(num_states);
  cur_prob(fst.Start()) = 1.0;
  for (int32 iter = 0; iter < num_iters; iter++) {
    for (int32 s = 0; s < num_states; s++) {
      double prob = cur_prob(s) * normalizing_factor(s);

      for (fst::ArcIterator<fst::StdVectorFst> aiter(fst, s); !aiter.Done();
           aiter.Next()) {
        const fst::StdArc &arc = aiter.Value();
        next_prob(arc.nextstate) += prob * exp(-arc.weight.Value());
      }
    }
    cur_prob.Swap(&next_prob);
    next_prob.SetZero();
    // Renormalize, beause the HMM won't sum to one even after the
    // previous normalization (due to final-probs).
    cur_prob.Scale(1.0 / cur_prob.Sum());
    avg_prob.AddVec(1.0 / num_iters, cur_prob);
  }

  Vector<BaseFloat> avg_prob_float(avg_prob);
  initial_probs_ = avg_prob_float;
  special_hmm_state_ = ComputeSpecialState(fst, avg_prob_float);
}

int32 NumStatesThatCanReach(const fst::StdVectorFst &fst,
                            int32 dest_state) {
  int32 num_states = fst.NumStates(),
      num_states_can_reach = 0;
  KALDI_ASSERT(dest_state >= 0 && dest_state < num_states);
  std::vector<bool> can_reach(num_states, false);
  std::vector<std::vector<int32> > reverse_transitions(num_states);
  for (int32 s = 0; s < num_states; s++)
    for (fst::ArcIterator<fst::StdVectorFst> aiter(fst, s); !aiter.Done();
         aiter.Next())
      reverse_transitions[aiter.Value().nextstate].push_back(s);
  std::vector<int32> queue;
  can_reach[dest_state] = true;
  queue.push_back(dest_state);
  num_states_can_reach++;
  while (!queue.empty()) {
    int32 state = queue.back();
    queue.pop_back();
    std::vector<int32>::const_iterator iter = reverse_transitions[state].begin(),
        end = reverse_transitions[state].end();
    for (; iter != end; ++iter) {
      int32 prev_state = *iter;
      if (!can_reach[prev_state]) {
        can_reach[prev_state] = true;
        queue.push_back(prev_state);
        num_states_can_reach++;
      }
    }
  }
  KALDI_ASSERT(num_states_can_reach >= 1 &&
               num_states_can_reach <= num_states);
  return num_states_can_reach;
}


int32 DenominatorGraph::ComputeSpecialState(
    const fst::StdVectorFst &fst,
    const Vector<BaseFloat> &initial_probs) {
  int32 num_states = initial_probs.Dim();
  std::vector<std::pair<BaseFloat, int32> > pairs(num_states);
  for (int32 i = 0; i < num_states; i++)
    pairs.push_back(std::pair<BaseFloat, int32>(-initial_probs(i), i));
  // the first element of each pair is the negative of the initial-prob,
  // so when we sort, the highest initial-prob will be first.
  std::sort(pairs.begin(), pairs.end());
  // this threshold of 0.75 is pretty arbitrary.  We reject any
  // state if it can't be reached by 75% of all other states.
  // In practice we think that states will either be reachable by
  // almost-all states, or almost-none (e.g. states that are active
  // only at utterance-beginning), so this threshold shouldn't
  // be too critical.
  int32 min_states_can_reach = 0.75 * num_states;
  for (int32 i = 0; i < num_states; i++) {
    int32 state = pairs[i].second;
    int32 n = NumStatesThatCanReach(fst, state);
    if (n < min_states_can_reach) {
      KALDI_WARN << "Rejecting state " << state << " as a 'special' HMM state "
                 << "(for renormalization in fwd-bkwd), because it's only "
                 << "reachable by " << n << " out of " << num_states
                 << " states.";
    } else {
      return state;
    }
  }
  KALDI_ERR << "Found no states that are reachable by at least "
            << min_states_can_reach << " out of " << num_states
            << " states.  This is unexpected.  Change the threshold";
  return -1;
}

void DenominatorGraph::GetNormalizationFst(const fst::StdVectorFst &ifst,
                                           fst::StdVectorFst *ofst) {
  KALDI_ASSERT(ifst.NumStates() == initial_probs_.Dim());
  if (&ifst != ofst)
    *ofst = ifst;
  int32 new_initial_state = ofst->AddState();
  Vector<BaseFloat> initial_probs(initial_probs_);
  for (int32 s = 0; s < initial_probs_.Dim(); s++) {
    BaseFloat initial_prob = initial_probs(s);
    fst::StdArc arc(0, 0, fst::TropicalWeight(-log(initial_prob)), s);
    ofst->AddArc(new_initial_state, arc);
    ofst->SetFinal(s, fst::TropicalWeight::One());
  }
  ofst->SetStart(new_initial_state);
  fst::RmEpsilon(ofst);
  fst::ArcSort(ofst, fst::ILabelCompare<fst::StdArc>());
}


void MapFstToPdfIdsPlusOne(const TransitionModel &trans_model,
                           fst::StdVectorFst *fst) {
  int32 num_states = fst->NumStates();
  for (int32 s = 0; s < num_states; s++) {
    for (fst::MutableArcIterator<fst::StdVectorFst> aiter(fst, s);
         !aiter.Done(); aiter.Next()) {
      fst::StdArc arc = aiter.Value();
      KALDI_ASSERT(arc.ilabel == arc.olabel);
      if (arc.ilabel > 0) {
        arc.ilabel = trans_model.TransitionIdToPdf(arc.ilabel) + 1;
        arc.olabel = arc.ilabel;
        aiter.SetValue(arc);
      }
    }
  }
}

void MinimizeAcceptorNoPush(fst::StdVectorFst *fst) {
  BaseFloat delta = 1.0e-05;
  fst::ArcMap(fst, fst::QuantizeMapper<fst::StdArc>(delta));
  fst::EncodeMapper<fst::StdArc> encoder(fst::kEncodeLabels | fst::kEncodeWeights,
                                         fst::ENCODE);
  fst::Encode(fst, &encoder);
  fst::AcceptorMinimize(fst);
  fst::Decode(fst, encoder);
}

void CreateDenominatorGraph(const ContextDependency &ctx_dep,
                            const TransitionModel &trans_model,
                            const fst::StdVectorFst &phone_lm_in,
                            fst::StdVectorFst *den_graph) {
  using fst::StdVectorFst;
  using fst::StdArc;
  KALDI_ASSERT(phone_lm_in.NumStates() != 0);
  fst::StdVectorFst phone_lm(phone_lm_in);

  int32 subsequential_symbol = trans_model.GetPhones().back() + 1;
  if (ctx_dep.CentralPosition() != ctx_dep.ContextWidth() - 1) {
    // note: this function only adds the subseq symbol to the input of what was
    // previously an acceptor, so we project, i.e. copy the ilabels to the
    // olabels
    AddSubsequentialLoop(subsequential_symbol, &phone_lm);
    fst::Project(&phone_lm, fst::PROJECT_INPUT);
  }
  std::vector<int32> disambig_syms;  // empty list of diambiguation symbols.
  fst::ContextFst<StdArc> cfst(subsequential_symbol, trans_model.GetPhones(),
                               disambig_syms, ctx_dep.ContextWidth(),
                               ctx_dep.CentralPosition());
  StdVectorFst context_dep_lm;
  fst::ComposeContextFst(cfst, phone_lm, &context_dep_lm);
  // at this point, context_dep_lm will have indexes into 'ilabels' as its
  // input symbol (representing context-dependent phones), and phones on its
  // output.  We don't need the phones, so we'll project.
  fst::Project(&context_dep_lm, fst::PROJECT_INPUT);

  KALDI_LOG << "Number of states in context-dependent LM FST is "
            << context_dep_lm.NumStates();

  std::vector<int32> disambig_syms_h; // disambiguation symbols on input side
  // of H -- will be empty.
  HTransducerConfig h_cfg;
  h_cfg.transition_scale = 0.0;  // we don't want transition probs.
  h_cfg.push_weights = false;  // there's nothing to push.

  StdVectorFst *h_fst = GetHTransducer(cfst.ILabelInfo(),
                                       ctx_dep,
                                       trans_model,
                                       h_cfg,
                                       &disambig_syms_h);
  KALDI_ASSERT(disambig_syms_h.empty());
  StdVectorFst transition_id_fst;
  TableCompose(*h_fst, context_dep_lm, &transition_id_fst);
  delete h_fst;

  BaseFloat self_loop_scale = 0.0;   // all transition-scales are 0.0; we aren't
  // using transition-probs here.
  bool reorder = true;  // more efficient in general; won't affect results.
  // add self-loops to the FST with transition-ids as its labels.
  AddSelfLoops(trans_model, disambig_syms_h, self_loop_scale, reorder,
               &transition_id_fst);
  // at this point transition_id_fst will have transition-ids as its ilabels and
  // context-dependent phones (indexes into ILabelInfo()) as its olabels.
  // Discard the context-dependent phones by projecting on the input, keeping
  // only the transition-ids.
  fst::Project(&transition_id_fst, fst::PROJECT_INPUT);

  MapFstToPdfIdsPlusOne(trans_model, &transition_id_fst);
  KALDI_LOG << "Number of states in transition-id FST is "
            << transition_id_fst.NumStates();

  fst::RmEpsilon(&transition_id_fst);
  KALDI_LOG << "Number of states in transition-id FST after "
            << "removing epsilons is "
            << transition_id_fst.NumStates();

  MinimizeAcceptorNoPush(&transition_id_fst);
  KALDI_LOG << "Number of states in transition-id FST after minimization is "
            << transition_id_fst.NumStates();
  *den_graph = transition_id_fst;

}


}  // namespace chain
}  // namespace kaldi