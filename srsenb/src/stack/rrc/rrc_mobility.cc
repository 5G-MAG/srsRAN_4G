/*
 * Copyright 2013-2019 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsenb/hdr/stack/rrc/rrc_mobility.h"
#include "srslte/common/bcd_helpers.h"
#include "srslte/common/common.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

namespace srsenb {

#define Info(fmt, ...) rrc_log->info("Mobility: " fmt, ##__VA_ARGS__)

using namespace asn1::rrc;

/*************************************************************************************************
 *         Convenience Functions to handle ASN1 MeasObjs/MeasId/ReportCfg/Cells/etc.
 ************************************************************************************************/

//! Convenience operator== overload to compare Fields of MeasObj/MeasId/ReportCfg/Cells/etc.
bool operator==(const cells_to_add_mod_s& lhs, const cells_to_add_mod_s& rhs)
{
  return lhs.cell_idx == rhs.cell_idx and lhs.pci == rhs.pci and
         lhs.cell_individual_offset == rhs.cell_individual_offset;
}
bool operator==(const meas_obj_to_add_mod_s& lhs, const meas_obj_to_add_mod_s& rhs)
{
  if (lhs.meas_obj_id != rhs.meas_obj_id or lhs.meas_obj.type() != lhs.meas_obj.type()) {
    return false;
  }
  auto &lhs_eutra = lhs.meas_obj.meas_obj_eutra(), &rhs_eutra = rhs.meas_obj.meas_obj_eutra();
  if (lhs_eutra.ext or rhs_eutra.ext) {
    printf("[%d] extension of measObjToAddMod not supported\n", __LINE__);
    return false;
  }

  if (lhs_eutra.offset_freq_present != rhs_eutra.offset_freq_present or
      (lhs_eutra.offset_freq_present and lhs_eutra.offset_freq != rhs_eutra.offset_freq)) {
    return false;
  }

  if (lhs_eutra.carrier_freq != rhs_eutra.carrier_freq or not(lhs_eutra.neigh_cell_cfg == rhs_eutra.neigh_cell_cfg) or
      lhs_eutra.presence_ant_port1 != rhs_eutra.presence_ant_port1 or
      lhs_eutra.allowed_meas_bw != rhs_eutra.allowed_meas_bw) {
    return false;
  }

  if (lhs_eutra.cells_to_add_mod_list.size() != rhs_eutra.cells_to_add_mod_list.size()) {
    return false;
  }

  auto cells_are_equal = [](const cells_to_add_mod_s& lhs, const cells_to_add_mod_s& rhs) { return lhs == rhs; };
  return std::equal(lhs_eutra.cells_to_add_mod_list.begin(),
                    lhs_eutra.cells_to_add_mod_list.end(),
                    rhs_eutra.cells_to_add_mod_list.begin(),
                    cells_are_equal);
}
bool operator==(const report_cfg_eutra_s& lhs, const report_cfg_eutra_s& rhs)
{
  if (lhs.ext or rhs.ext) {
    printf("[%d] extension of reportCfgToAddMod not supported\n", __LINE__);
    return false;
  }
  if (lhs.trigger_type.type() != rhs.trigger_type.type()) {
    return false;
  }
  if (lhs.trigger_type.type().value == report_cfg_eutra_s::trigger_type_c_::types_opts::event) {
    auto &lhs_ev = lhs.trigger_type.event(), &rhs_ev = rhs.trigger_type.event();
    if (lhs_ev.hysteresis != rhs_ev.hysteresis or lhs_ev.time_to_trigger != rhs_ev.time_to_trigger or
        lhs_ev.event_id.type() != rhs_ev.event_id.type()) {
      return false;
    }
    if (lhs_ev.event_id.type().value != eutra_event_s::event_id_c_::types_opts::event_a3) {
      printf("[%d] event type != A3 of reportCfgToAddMod not supported\n", __LINE__);
      return false;
    }
    if (lhs_ev.event_id.event_a3().report_on_leave != rhs_ev.event_id.event_a3().report_on_leave or
        lhs_ev.event_id.event_a3().a3_offset != rhs_ev.event_id.event_a3().a3_offset) {
      return false;
    }
  } else {
    if (lhs.trigger_type.periodical().purpose != rhs.trigger_type.periodical().purpose) {
      return false;
    }
  }
  return lhs.trigger_quant == rhs.trigger_quant and lhs.report_quant == rhs.report_quant and
         lhs.max_report_cells == rhs.max_report_cells and lhs.report_interv == rhs.report_interv and
         lhs.report_amount == rhs.report_amount;
}
bool operator==(const report_cfg_to_add_mod_s& lhs, const report_cfg_to_add_mod_s& rhs)
{
  if (lhs.report_cfg_id != rhs.report_cfg_id or lhs.report_cfg.type() != rhs.report_cfg.type()) {
    return false;
  }
  return lhs.report_cfg.report_cfg_eutra() == rhs.report_cfg.report_cfg_eutra();
}
bool operator==(const meas_id_to_add_mod_s& lhs, const meas_id_to_add_mod_s& rhs)
{
  return lhs.meas_id == rhs.meas_id and lhs.meas_obj_id == rhs.meas_obj_id and lhs.report_cfg_id == rhs.report_cfg_id;
}

namespace rrc_details {

//! extract cell id from ECI
uint32_t eci_to_cellid(uint32_t eci)
{
  return eci & 0xFFu;
}

//! convenience function overload to extract Id from MeasObj/MeasId/ReportCfg/Cells
constexpr uint8_t get_id(const cells_to_add_mod_s& obj)
{
  return obj.cell_idx;
}
constexpr uint8_t get_id(const meas_obj_to_add_mod_s& obj)
{
  return obj.meas_obj_id;
}
constexpr uint8_t get_id(const report_cfg_to_add_mod_s& obj)
{
  return obj.report_cfg_id;
}
constexpr uint8_t get_id(const meas_id_to_add_mod_s& obj)
{
  return obj.meas_id;
}

//! convenience function overload to print MeasObj/MeasId/etc. fields
std::string to_string(const cells_to_add_mod_s& obj)
{
  char buf[128];
  std::snprintf(
      buf, 128, "{cell_idx: %d, pci: %d, offset: %d}", obj.cell_idx, obj.pci, obj.cell_individual_offset.to_number());
  return {buf};
}

//! meas field comparison based on ID solely
template <typename T>
struct field_id_cmp {
  using IdType = decltype(get_id(T{}));
  bool operator()(const T& lhs, const T& rhs) const { return get_id(lhs) < get_id(rhs); }
  bool operator()(const T& lhs, IdType id) const { return get_id(lhs) < id; }
};
using cell_id_cmp     = field_id_cmp<cells_to_add_mod_s>;
using meas_obj_id_cmp = field_id_cmp<meas_obj_to_add_mod_s>;

//! Find MeasObj with same earfcn
meas_obj_to_add_mod_s* find_meas_obj(meas_obj_to_add_mod_list_l& l, uint32_t earfcn)
{
  auto same_earfcn = [earfcn](const meas_obj_to_add_mod_s& obj) {
    return obj.meas_obj.type().value == meas_obj_to_add_mod_s::meas_obj_c_::types_opts::meas_obj_eutra and
           obj.meas_obj.meas_obj_eutra().carrier_freq == earfcn;
  };
  auto it = std::find_if(l.begin(), l.end(), same_earfcn);
  if (it == l.end()) {
    return nullptr;
  }
  return it;
}

/** Finds a cell in this->objects based on cell_id and frequency
 *  return pair of (meas_obj,cell_obj). If no cell has frequency==earfcn, meas_obj=nullptr
 */
std::pair<meas_obj_to_add_mod_s*, cells_to_add_mod_s*>
find_cell(meas_obj_to_add_mod_list_l& l, uint32_t earfcn, uint8_t cell_id)
{
  // find meas_obj with same earfcn
  meas_obj_to_add_mod_s* obj = rrc_details::find_meas_obj(l, earfcn);
  if (obj == nullptr) {
    return {nullptr, nullptr};
  }
  // find cell with same id
  auto& cells = obj->meas_obj.meas_obj_eutra().cells_to_add_mod_list;
  auto  it    = std::lower_bound(cells.begin(), cells.end(), cell_id, rrc_details::cell_id_cmp{});
  if (it == cells.end() or it->cell_idx != cell_id) {
    return {obj, nullptr};
  }
  return {obj, it};
}

/**
 * Section 5.5.2.5
 * Description: Adds MeasObjtoAddMod to MeasCfg object
 */
meas_obj_to_add_mod_s* meascfg_add_meas_obj(meas_cfg_s* meas_cfg, const meas_obj_to_add_mod_s& meas_obj)
{
  meas_cfg->meas_obj_to_add_mod_list_present = true;
  meas_obj_to_add_mod_list_l& l              = meas_cfg->meas_obj_to_add_mod_list;

  // search for meas_obj by obj_id to ensure uniqueness (assume sorted)
  auto found_it = std::lower_bound(l.begin(), l.end(), meas_obj.meas_obj_id, meas_obj_id_cmp{});
  // TODO: Assert dl_earfcn is the same
  if (found_it == l.end()) {
    l.push_back({});
    found_it              = &l.back();
    found_it->meas_obj_id = meas_obj.meas_obj_id;
  }

  auto& target_eutra               = found_it->meas_obj.set_meas_obj_eutra();
  auto& src_eutra                  = meas_obj.meas_obj.meas_obj_eutra();
  target_eutra.carrier_freq        = src_eutra.carrier_freq;
  target_eutra.offset_freq_present = src_eutra.offset_freq_present;
  target_eutra.offset_freq         = src_eutra.offset_freq;
  target_eutra.allowed_meas_bw     = src_eutra.allowed_meas_bw;
  target_eutra.presence_ant_port1  = src_eutra.presence_ant_port1;
  target_eutra.neigh_cell_cfg      = src_eutra.neigh_cell_cfg;
  // do not add cellsToAddModList, blacCells, whiteCells, etc. according to (5.5.2.5 1|1|1)

  return found_it;
}

/*
 * Algorithm to compare differences between Meas Fields. This is used for MeasObjs, Cells, ReportConfig, MeasId (and
 * more in the future)
 * Returns outcome==same_id, if two fields were spotted with same id, "id_removed" if an id was removed from the target
 * "id_added" if an id was added to the target, and complete when the iteration is over
 */
enum class diff_outcome_t { same_id, id_removed, id_added, complete };
template <typename Container>
struct compute_diff_generator {
  using const_iterator = typename Container::const_iterator;
  struct result_t {
    diff_outcome_t outcome;
    const_iterator src_it;
    const_iterator target_it;
  };

  compute_diff_generator(const Container& src, const Container& target) :
    src_it(src.begin()),
    src_end(src.end()),
    target_it(target.begin()),
    target_end(target.end())
  {
  }

  result_t next()
  {
    bool src_left    = src_it != src_end;
    bool target_left = target_it != target_end;
    if (not src_left and not target_left) {
      return {diff_outcome_t::complete, nullptr, nullptr};
    }
    if (not target_left or (src_left and get_id(*src_it) < get_id(*target_it))) {
      // an object has been removed from the target
      return {diff_outcome_t::id_removed, src_it++, target_it};
    }
    if (not src_left or (target_left and get_id(*src_it) > get_id(*target_it))) {
      // a new object has been added to target
      return {diff_outcome_t::id_added, src_it, target_it++};
    }
    // Same ID
    return {diff_outcome_t::same_id, src_it++, target_it++};
  }

private:
  const_iterator src_it, src_end, target_it, target_end;
};

template <typename Container, typename IdType = decltype(get_id(*Container{}.begin()))>
IdType find_id_gap(const Container& c)
{
  auto prev_it = c.begin();
  if (prev_it != c.end() and get_id(*prev_it) == 1) {
    auto it = prev_it;
    for (++it; it != c.end(); prev_it = it, ++it) {
      if (get_id(*it) > get_id(*prev_it) + 1) {
        break;
      }
    }
  }
  return (prev_it == c.end()) ? 1 : get_id(*prev_it) + 1; // starts at 1.
}

} // namespace rrc_details

/*************************************************************************************************
 *                                  var_meas_cfg_t class
 ************************************************************************************************/

//! Add cell parsed in configuration file to the varMeasCfg
std::tuple<bool, var_meas_cfg_t::meas_obj_t*, var_meas_cfg_t::meas_cell_t*>
var_meas_cfg_t::add_cell_cfg(const meas_cell_cfg_t& cellcfg)
{
  using namespace rrc_details;
  bool inserted_flag = true;

  // FIXME: cellcfg.cell_id is the ECI
  uint32_t         cell_id = rrc_details::eci_to_cellid(cellcfg.cell_id);
  q_offset_range_e offset;
  asn1::number_to_enum(offset, (int8_t)cellcfg.q_offset); // FIXME: What's the difference

  std::pair<meas_obj_t*, meas_cell_t*> ret = rrc_details::find_cell(var_meas.meas_obj_list, cellcfg.earfcn, cell_id);
  cells_to_add_mod_s                   new_cell;
  new_cell.cell_idx               = cell_id;
  new_cell.cell_individual_offset = offset;
  new_cell.pci                    = cellcfg.pci;

  if (ret.first != nullptr) {
    // there are cells with the same earfcn at least.
    if (ret.second != nullptr) {
      // the cell already existed.
      if (ret.second->pci != cellcfg.pci or ret.second->cell_individual_offset != offset) {
        // members of cell were updated
        *ret.second = new_cell;
      } else {
        inserted_flag = false;
      }
    } else {
      // cell_id not found. create new cell
      auto& cell_list = ret.first->meas_obj.meas_obj_eutra().cells_to_add_mod_list;
      cell_list.push_back(new_cell);
      std::sort(cell_list.begin(), cell_list.end(), rrc_details::cell_id_cmp{});
      // find cell in new position
      ret.second = std::lower_bound(cell_list.begin(), cell_list.end(), new_cell.cell_idx, rrc_details::cell_id_cmp{});
    }
  } else {
    // no measobj has been found with same earfcn, create a new one
    meas_obj_t new_obj;
    new_obj.meas_obj_id                = rrc_details::find_id_gap(var_meas.meas_obj_list);
    asn1::rrc::meas_obj_eutra_s& eutra = new_obj.meas_obj.set_meas_obj_eutra();
    eutra.carrier_freq                 = cellcfg.earfcn;
    eutra.allowed_meas_bw.value        = asn1::rrc::allowed_meas_bw_e::mbw6; // FIXME: What value to add here?
    eutra.neigh_cell_cfg.from_number(1);                                     // FIXME: What value?
    eutra.offset_freq_present = true;
    // TODO: Assert that q_offset is in ms
    asn1::number_to_enum(eutra.offset_freq, cellcfg.q_offset);
    eutra.cells_to_add_mod_list_present = true;
    eutra.cells_to_add_mod_list.push_back(new_cell);
    var_meas.meas_obj_list.push_back(new_obj);
    std::sort(var_meas.meas_obj_list.begin(), var_meas.meas_obj_list.end(), rrc_details::meas_obj_id_cmp{});
    // get measObj in new position
    ret.first  = std::lower_bound(var_meas.meas_obj_list.begin(),
                                 var_meas.meas_obj_list.end(),
                                 new_obj.meas_obj_id,
                                 rrc_details::meas_obj_id_cmp{});
    ret.second = &ret.first->meas_obj.meas_obj_eutra().cells_to_add_mod_list.back();
  }

  if (inserted_flag) {
    var_meas.meas_obj_list_present = true;
  }

  return {inserted_flag, ret.first, ret.second};
}

report_cfg_to_add_mod_s* var_meas_cfg_t::add_report_cfg(const report_cfg_eutra_s& reportcfg)
{
  rrc_details::field_id_cmp<report_cfg_to_add_mod_s> cmp{};
  report_cfg_to_add_mod_s                            new_rep;
  new_rep.report_cfg_id                     = rrc_details::find_id_gap(var_meas.report_cfg_list);
  new_rep.report_cfg.set_report_cfg_eutra() = reportcfg;

  var_meas.report_cfg_list_present = true;
  var_meas.report_cfg_list.push_back(new_rep);
  std::sort(var_meas.report_cfg_list.begin(), var_meas.report_cfg_list.end(), cmp);
  return std::lower_bound(var_meas.report_cfg_list.begin(), var_meas.report_cfg_list.end(), new_rep.report_cfg_id, cmp);
}

meas_id_to_add_mod_s* var_meas_cfg_t::add_measid_cfg(uint8_t measobjid, uint8_t measrepid)
{
  // ensure MeasObjId and ReportCfgId already exist
  auto objit = std::lower_bound(
      var_meas.meas_obj_list.begin(), var_meas.meas_obj_list.end(), measobjid, rrc_details::meas_obj_id_cmp{});
  if (objit == var_meas.meas_obj_list.end() or objit->meas_obj_id != measobjid) {
    ERROR("Failed to add MeasId because MeasObjId=%d is not found.\n", measobjid);
    return nullptr;
  }
  auto repit = std::lower_bound(var_meas.report_cfg_list.begin(),
                                var_meas.report_cfg_list.end(),
                                measrepid,
                                rrc_details::field_id_cmp<report_cfg_to_add_mod_s>{});
  if (repit == var_meas.report_cfg_list.end() or repit->report_cfg_id != measrepid) {
    ERROR("Failed to add MeasId because ReportCfgId=%d is not found.\n", measrepid);
    return nullptr;
  }
  rrc_details::field_id_cmp<meas_id_to_add_mod_s> cmp{};
  meas_id_to_add_mod_s                            new_measid;
  new_measid.report_cfg_id = measrepid;
  new_measid.meas_obj_id   = measobjid;
  new_measid.meas_id       = rrc_details::find_id_gap(var_meas.meas_id_list);

  var_meas.meas_id_list_present = true;
  var_meas.meas_id_list.push_back(new_measid);
  std::sort(var_meas.meas_id_list.begin(), var_meas.meas_id_list.end(), cmp);
  return std::lower_bound(var_meas.meas_id_list.begin(), var_meas.meas_id_list.end(), new_measid.meas_id, cmp);
}

bool var_meas_cfg_t::compute_diff_meas_cfg(const var_meas_cfg_t& target_cfg, asn1::rrc::meas_cfg_s* meas_cfg) const
{
  *meas_cfg = {};
  // TODO: Create a flag to disable changing the "this" members (useful for transparent container)
  // Set a MeasConfig in the RRC Connection Reconfiguration for HO.
  compute_diff_meas_objs(target_cfg, meas_cfg);
  compute_diff_report_cfgs(target_cfg, meas_cfg);
  compute_diff_meas_ids(target_cfg, meas_cfg);
  //  deltaconfig_meas_quantity_config(target_cfg, meas_cfg);
  meas_cfg->meas_gap_cfg_present         = false; // NOTE: we do not support inter-freq. HO
  meas_cfg->s_measure_present            = false; // NOTE: We do not support SCells
  meas_cfg->pre_regist_info_hrpd_present = false; // NOTE: not supported
  meas_cfg->speed_state_pars_present     = false; // NOTE: not supported

  bool diff = meas_cfg->meas_obj_to_add_mod_list_present;
  diff |= meas_cfg->meas_obj_to_rem_list_present;
  diff |= meas_cfg->report_cfg_to_add_mod_list_present;
  diff |= meas_cfg->report_cfg_to_rem_list_present;
  diff |= meas_cfg->meas_id_to_add_mod_list_present;
  diff |= meas_cfg->meas_id_to_rem_list_present;
  diff |= meas_cfg->quant_cfg_present;
  diff |= meas_cfg->meas_gap_cfg_present;
  diff |= meas_cfg->s_measure_present;
  diff |= meas_cfg->pre_regist_info_hrpd_present;
  diff |= meas_cfg->speed_state_pars_present;
  return diff;
}

//! adds all the cells that got updated to MeasCfg.
void var_meas_cfg_t::compute_diff_cells(const meas_obj_eutra_s& target_it,
                                        const meas_obj_eutra_s& src_it,
                                        meas_obj_to_add_mod_s*  added_obj) const
{
  rrc_details::compute_diff_generator<cells_to_add_mod_list_l> diffs{src_it.cells_to_add_mod_list,
                                                                     target_it.cells_to_add_mod_list};

  meas_obj_eutra_s* eutra_obj = &added_obj->meas_obj.meas_obj_eutra();
  while (true) {
    auto result = diffs.next();
    switch (result.outcome) {
      case rrc_details::diff_outcome_t::complete:
        return;
      case rrc_details::diff_outcome_t::id_removed:
        // case "entry with matching cellIndex exists in cellsToRemoveList
        Info("UE can now cease to measure activity of cell %s.\n", rrc_details::to_string(*result.target_it).c_str());
        eutra_obj->cells_to_rem_list_present = true;
        eutra_obj->cells_to_rem_list.push_back(result.src_it->cell_idx);
        break;
      case rrc_details::diff_outcome_t::id_added:
        // case "entry with matching cellIndex doesn't exist in cellsToAddModList"
        Info("UE has now to measure activity of %s.\n", rrc_details::to_string(*result.target_it).c_str());
        eutra_obj->cells_to_add_mod_list_present = true;
        eutra_obj->cells_to_add_mod_list.push_back(*result.target_it);
        break;
      case rrc_details::diff_outcome_t::same_id:
        if (not(*result.src_it == *result.target_it)) {
          // case "entry with matching cellIndex exists in cellsToAddModList"
          Info("UE has now to measure activity of %s with updated params.\n",
               rrc_details::to_string(*result.target_it).c_str());
          eutra_obj->cells_to_add_mod_list_present = true;
          eutra_obj->cells_to_add_mod_list.push_back(*result.target_it);
        }
        break;
    }
  }
}

/**
 * Section 5.5.2.4/5, Measurement Object removal and addition/modification
 * Description: compute diff between target_cfg and var_meas -> depending on diff, add/remove/update meas_obj in
 * meas_cfg
 */
void var_meas_cfg_t::compute_diff_meas_objs(const var_meas_cfg_t& target_cfg, meas_cfg_s* meas_cfg) const
{
  if (not target_cfg.var_meas.meas_obj_list_present) {
    return;
  }
  // TODO: black cells and white cells
  rrc_details::compute_diff_generator<meas_obj_to_add_mod_list_l> diffs{var_meas.meas_obj_list,
                                                                        target_cfg.var_meas.meas_obj_list};

  while (true) {
    auto result = diffs.next();
    switch (result.outcome) {
      case rrc_details::diff_outcome_t::complete:
        return;
      case rrc_details::diff_outcome_t::id_removed:
        // case "entry with matching cellIndex exists in cellsToRemoveList
        Info("UE can cease to measure activity in frequency earfcn=%d.\n",
             result.src_it->meas_obj.meas_obj_eutra().carrier_freq);
        meas_cfg->meas_obj_to_rem_list_present = true;
        meas_cfg->meas_obj_to_rem_list.push_back(result.src_it->meas_obj_id);
        break;
      case rrc_details::diff_outcome_t::id_added: {
        // case "entry with matching measObjectId doesn't exist in measObjToAddModList"
        Info("HO: UE has now to measure activity of new frequency earfcn=%d.\n",
             result.target_it->meas_obj.meas_obj_eutra().carrier_freq);
        auto& target_eutra = result.target_it->meas_obj.meas_obj_eutra();
        auto& added_eutra  = rrc_details::meascfg_add_meas_obj(meas_cfg, *result.target_it)->meas_obj.meas_obj_eutra();
        // add all cells in measCfg
        for (const cells_to_add_mod_s& cell_it : target_eutra.cells_to_add_mod_list) {
          added_eutra.cells_to_add_mod_list_present = true;
          added_eutra.cells_to_add_mod_list.push_back(cell_it);
        }
      } break;
      case rrc_details::diff_outcome_t::same_id:
        // case "entry with matching measObjectId exists in measObjToAddModList"
        bool are_equal = *result.src_it == *result.target_it;
        if (not are_equal) {
          // if we found a difference in obj IDs
          meas_obj_to_add_mod_s* added_obj = rrc_details::meascfg_add_meas_obj(meas_cfg, *result.target_it);
          // Add cells if there were changes.
          compute_diff_cells(
              result.target_it->meas_obj.meas_obj_eutra(), result.src_it->meas_obj.meas_obj_eutra(), added_obj);
        }
        break;
    }
  }
}

/**
 * Section 5.5.2.6/7 - Reporting configuration removal and addition/modification
 */
void var_meas_cfg_t::compute_diff_report_cfgs(const var_meas_cfg_t& target_cfg, asn1::rrc::meas_cfg_s* meas_cfg) const
{
  if (not target_cfg.var_meas.report_cfg_list_present) {
    return;
  }
  rrc_details::compute_diff_generator<report_cfg_to_add_mod_list_l> diffs{var_meas.report_cfg_list,
                                                                          target_cfg.var_meas.report_cfg_list};

  while (true) {
    auto result = diffs.next();
    switch (result.outcome) {
      case rrc_details::diff_outcome_t::complete:
        return;
      case rrc_details::diff_outcome_t::id_removed:
        meas_cfg->report_cfg_to_rem_list_present = true;
        meas_cfg->report_cfg_to_rem_list.push_back(result.src_it->report_cfg_id);
        break;
      case rrc_details::diff_outcome_t::id_added:
        meas_cfg->report_cfg_to_add_mod_list_present = true;
        meas_cfg->report_cfg_to_add_mod_list.push_back(*result.target_it);
        break;
      case rrc_details::diff_outcome_t::same_id:
        if (not(*result.src_it == *result.target_it)) {
          meas_cfg->report_cfg_to_add_mod_list_present = true;
          meas_cfg->report_cfg_to_add_mod_list.push_back(*result.target_it);
        }
        break;
    }
  }
}

void var_meas_cfg_t::compute_diff_meas_ids(const var_meas_cfg_t& target_cfg, asn1::rrc::meas_cfg_s* meas_cfg) const
{
  if (not target_cfg.var_meas.meas_id_list_present) {
    return;
  }
  rrc_details::compute_diff_generator<meas_id_to_add_mod_list_l> diffs{var_meas.meas_id_list,
                                                                       target_cfg.var_meas.meas_id_list};

  while (true) {
    auto result = diffs.next();
    switch (result.outcome) {
      case rrc_details::diff_outcome_t::complete:
        return;
      case rrc_details::diff_outcome_t::id_removed:
        meas_cfg->meas_id_to_rem_list_present = true;
        meas_cfg->meas_id_to_rem_list.push_back(result.src_it->meas_id);
        break;
      case rrc_details::diff_outcome_t::id_added:
        meas_cfg->meas_id_to_add_mod_list_present = true;
        meas_cfg->meas_id_to_add_mod_list.push_back(*result.target_it);
        break;
      case rrc_details::diff_outcome_t::same_id:
        if (not(*result.src_it == *result.target_it)) {
          meas_cfg->meas_id_to_add_mod_list_present = true;
          meas_cfg->meas_id_to_add_mod_list.push_back(*result.target_it);
        }
        break;
    }
  }
}

/*************************************************************************************************
 *                                  mobility_cfg class
 ************************************************************************************************/

rrc::mobility_cfg::mobility_cfg(rrc* outer_rrc) : rrc_enb(outer_rrc)
{
  var_meas_cfg_t var_meas{outer_rrc->rrc_log};

  if (rrc_enb->cfg.meas_cfg_present) {
    // inserts all neighbor cells
    for (meas_cell_cfg_t& meascell : rrc_enb->cfg.meas_cfg.meas_cells) {
      var_meas.add_cell_cfg(meascell);
    }

    // insert all report cfgs
    for (const report_cfg_eutra_s& reportcfg : rrc_enb->cfg.meas_cfg.meas_reports) {
      var_meas.add_report_cfg(reportcfg);
    }

    // insert all meas ids
    // FIXME: add this to the parser
    for (const auto& measobj : var_meas.meas_objs()) {
      var_meas.add_measid_cfg(measobj.meas_obj_id, var_meas.rep_cfgs().begin()->report_cfg_id);
    }
  }

  current_meas_cfg = std::make_shared<var_meas_cfg_t>(var_meas);
}

/*************************************************************************************************
 *                                  rrc_mobility class
 ************************************************************************************************/

rrc::ue::rrc_mobility::rrc_mobility(rrc::ue* outer_ue) :
  rrc_ue(outer_ue),
  rrc_enb(outer_ue->parent),
  cfg(outer_ue->parent->enb_mobility_cfg.get()),
  pool(outer_ue->pool),
  rrc_log(outer_ue->parent->rrc_log)
{
  ue_var_meas = std::make_shared<var_meas_cfg_t>(outer_ue->parent->rrc_log);
}

//! Method to add Mobility Info to a RRC Connection Reconfiguration Message
bool rrc::ue::rrc_mobility::fill_conn_recfg_msg(asn1::rrc::rrc_conn_recfg_r8_ies_s* conn_recfg)
{
  // only reconfigure meas_cfg if no handover is occurring
  if (mobility_proc.is_busy()) {
    return false;
  }

  // Check if there has been any update
  if (ue_var_meas.get() == cfg->current_meas_cfg.get()) {
    return false;
  }

  asn1::rrc::meas_cfg_s* meas_cfg = &conn_recfg->meas_cfg;
  bool                   updated  = ue_var_meas->compute_diff_meas_cfg(*cfg->current_meas_cfg, meas_cfg);
  // update ue var meas
  ue_var_meas = cfg->current_meas_cfg;

  if (updated) {
    conn_recfg->meas_cfg_present = true;
    return true;
  }
  return false;
}

} // namespace srsenb