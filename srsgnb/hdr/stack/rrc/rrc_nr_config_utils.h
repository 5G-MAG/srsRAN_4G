/**
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#ifndef SRSRAN_RRC_NR_CONFIG_DEFAULT_H
#define SRSRAN_RRC_NR_CONFIG_DEFAULT_H

#include "rrc_nr_config.h"

namespace srsenb {

void generate_default_nr_cell(rrc_cell_cfg_nr_t& cell);

int set_derived_nr_cell_params(bool is_sa, rrc_cell_cfg_nr_t& cell);

int check_nr_cell_cfg_valid(const rrc_cell_cfg_nr_t& cell, bool is_sa);

} // namespace srsenb

#endif // SRSRAN_RRC_NR_CONFIG_DEFAULT_H
