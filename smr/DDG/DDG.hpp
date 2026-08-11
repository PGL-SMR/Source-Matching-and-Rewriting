#pragma once

#include "Attributes.hpp"
#include "Automaton/Automaton.hpp"
#include "Automaton/Hash.hpp"
#include "Automaton/State.hpp"
#include "Globals.hpp"
#include "Input.hpp"
#include "Match.hpp"
#include "Memory.hpp"
#include "Pattern.hpp"
#include "Regions.hpp"
#include "StringSet.hpp"
#include "mlir/IR/Block.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/vector.hpp>
#include <vector>

namespace ddg {

/// \brief Data Dependence Graph abstraction.
///
/// This class abstracts every DDG-related process, from building the DDG to
/// running the pattern matching algorithm.
class DDG : public Automaton {
private:
  friend class boost::serialization::access;

  template <class Archive>
  void save(Archive &Ar, const unsigned int /*unused*/) const {
    // Convert llvm object to std object.
    std::map<int, std::set<int>> StdBuckets;
    for (const auto &Bucket : Buckets)
      StdBuckets.insert(Bucket);

    Ar &boost::serialization::base_object<Automaton>(*this);
    Ar & Hasher;
    Ar & Patterns;
    Ar & StdBuckets;
  }

  template <class Archive>
  void load(Archive &Ar, const unsigned int /*unused*/) {
    std::map<int, std::set<int>> StdBuckets;
    Ar &boost::serialization::base_object<Automaton>(*this);
    Ar & Hasher;
    Ar & Patterns;
    Ar & StdBuckets;

    // Convert std object to llvm object.
    for (auto &Bucket : StdBuckets)
      Buckets.insert(Bucket);
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()

  /// \brief Hash to convert strings to integers (and vice-versa).
  Hash Hasher;

  /// \brief Global operations manager.
  ddg::Globals Globals;

  /// \brief IR Regions IDs manager.
  ddg::Regions Regions;

  /// \brief IR Memory dependency manager.
  ddg::Memory Memory;

  /// \brief IR attributes manager.
  ddg::Attributes Attributes;

  /// \brief List of DDG patterns in the automaton.
  std::vector<DdgPattern> Patterns;

  /// \brief Map from final states to DDG patterns.
  llvm::DenseMap<int, std::set<int>> Buckets;

  /// \brief Match a DDG input against all DDG patterns.
  ///
  /// \param Input DDG input.
  ///
  /// \return List of matches.
  std::vector<Match> match(Input &Input);

  /// \brief Add a DDG pattern to the automaton.
  ///
  /// \param Pattern DDG pattern.
  /// \param EntryBlock Entry block of the DDG pattern.
  void addPattern(DdgPattern &Pattern, mlir::Block *EntryBlock);

public:
  /// \brief Stringify a list of attributes.
  static std::string stringify(mlir::NamedAttrList &&Attrs);

  /// \brief Stringify a operation.
  ///
  /// \param Op Operation to stringify.
  /// \param EntryBlock Pattern entry block.
  ///
  /// \return Stringified operation.
  StringSet stringify(mlir::Operation *Op, mlir::Block *EntryBlock);

  /// \brief Stringify a value.
  ///
  /// If the entry block is set, and the value originated in the entry block,
  /// the value is stringified as a wilcard.
  ///
  /// \param Op Operation to stringify.
  /// \param EntryBlock Pattern entry block.
  ///
  /// \return Stringified value.
  StringSet stringify(mlir::Value &Value, mlir::Block *EntryBlock);

  /// \brief Dump all DDG patterns.
  void dump();

  /// \brief Build the DDG automaton.
  ///
  /// \param PatternRoots List of pattern IDs and root RDOs.
  ///
  /// \return Error code.
  int build(std::map<int, mlir::Operation *> &&PatternRoots);

  /// \brief Run the pattern matching algorithm.
  ///
  /// \param Candidates Set of candidate inputs to be matched.
  ///
  /// \return List of DDG matches.
  std::vector<Match> run(std::set<mlir::Operation *> &&Candidates);
};

} // namespace ddg
