/* Copyright (c) 2014, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "dd_table_share.h"

#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "dd/cache/dictionary_client.h"       // dd::cache::Dictionary_client
#include "dd/collection.h"
#include "dd/dd_schema.h"                     // dd::schema_exists
#include "dd/dd_table.h"                      // dd::abstract_table_type
#include "dd/dd_tablespace.h"                 // dd::get_tablespace_name
// TODO: Avoid exposing dd/impl headers in public files.
#include "dd/impl/utils.h"                    // dd::eat_str
#include "dd/properties.h"                    // dd::Properties
#include "dd/string_type.h"
#include "dd/types/abstract_table.h"
#include "dd/types/column.h"                  // dd::enum_column_types
#include "dd/types/column_type_element.h"     // dd::Column_type_element
#include "dd/types/index.h"                   // dd::Index
#include "dd/types/index_element.h"           // dd::Index_element
#include "dd/types/partition.h"               // dd::Partition
#include "dd/types/partition_value.h"         // dd::Partition_value
#include "dd/types/table.h"                   // dd::Table
#include "default_values.h"                   // prepare_default_value_buffer...
#include "field.h"
#include "handler.h"
#include "hash.h"
#include "key.h"
#include "log.h"                              // sql_print_error
#include "my_base.h"
#include "my_bitmap.h"
#include "my_compare.h"
#include "my_compiler.h"
#include "my_config.h"
#include "my_dbug.h"
#include "mysql/plugin.h"
#include "mysql/psi/psi_base.h"
#include "mysql_com.h"
#include "mysqld_error.h"
#include "partition_element.h"                // partition_element
#include "partition_info.h"                   // partition_info
#include "session_tracker.h"
#include "sql_bitmap.h"
#include "sql_class.h"                        // THD
#include "sql_const.h"
#include "sql_error.h"
#include "sql_list.h"
#include "sql_partition.h"                    // generate_partition_syntax
#include "sql_plugin.h"                       // plugin_unlock
#include "sql_plugin_ref.h"
#include "sql_table.h"                        // primary_key_name
#include "strfunc.h"                          // lex_cstring_handle
#include "system_variables.h"
#include "table.h"
#include "typelib.h"

namespace dd {
class View;
}  // namespace dd


enum_field_types dd_get_old_field_type(dd::enum_column_types type)
{
  switch (type)
  {
  case dd::enum_column_types::DECIMAL:
    return MYSQL_TYPE_DECIMAL;

  case dd::enum_column_types::TINY:
    return MYSQL_TYPE_TINY;

  case dd::enum_column_types::SHORT:
    return MYSQL_TYPE_SHORT;

  case dd::enum_column_types::LONG:
    return MYSQL_TYPE_LONG;

  case dd::enum_column_types::FLOAT:
    return MYSQL_TYPE_FLOAT;

  case dd::enum_column_types::DOUBLE:
    return MYSQL_TYPE_DOUBLE;

  case dd::enum_column_types::TYPE_NULL:
    return MYSQL_TYPE_NULL;

  case dd::enum_column_types::TIMESTAMP:
    return MYSQL_TYPE_TIMESTAMP;

  case dd::enum_column_types::LONGLONG:
    return MYSQL_TYPE_LONGLONG;

  case dd::enum_column_types::INT24:
    return MYSQL_TYPE_INT24;

  case dd::enum_column_types::DATE:
    return MYSQL_TYPE_DATE;

  case dd::enum_column_types::TIME:
    return MYSQL_TYPE_TIME;

  case dd::enum_column_types::DATETIME:
    return MYSQL_TYPE_DATETIME;

  case dd::enum_column_types::YEAR:
    return MYSQL_TYPE_YEAR;

  case dd::enum_column_types::NEWDATE:
    return MYSQL_TYPE_NEWDATE;

  case dd::enum_column_types::VARCHAR:
    return MYSQL_TYPE_VARCHAR;

  case dd::enum_column_types::BIT:
    return MYSQL_TYPE_BIT;

  case dd::enum_column_types::TIMESTAMP2:
    return MYSQL_TYPE_TIMESTAMP2;

  case dd::enum_column_types::DATETIME2:
    return MYSQL_TYPE_DATETIME2;

  case dd::enum_column_types::TIME2:
    return MYSQL_TYPE_TIME2;

  case dd::enum_column_types::NEWDECIMAL:
    return MYSQL_TYPE_NEWDECIMAL;

  case dd::enum_column_types::ENUM:
    return MYSQL_TYPE_ENUM;

  case dd::enum_column_types::SET:
    return MYSQL_TYPE_SET;

  case dd::enum_column_types::TINY_BLOB:
    return MYSQL_TYPE_TINY_BLOB;

  case dd::enum_column_types::MEDIUM_BLOB:
    return MYSQL_TYPE_MEDIUM_BLOB;

  case dd::enum_column_types::LONG_BLOB:
    return MYSQL_TYPE_LONG_BLOB;

  case dd::enum_column_types::BLOB:
    return MYSQL_TYPE_BLOB;

  case dd::enum_column_types::VAR_STRING:
    return MYSQL_TYPE_VAR_STRING;

  case dd::enum_column_types::STRING:
    return MYSQL_TYPE_STRING;

  case dd::enum_column_types::GEOMETRY:
    return MYSQL_TYPE_GEOMETRY;

  case dd::enum_column_types::JSON:
    return MYSQL_TYPE_JSON;

  default:
    DBUG_ASSERT(!"Should not hit here"); /* purecov: deadcode */
  }

  return MYSQL_TYPE_LONG;
}


/** For enum in dd::Index */
static enum ha_key_alg dd_get_old_index_algorithm_type(dd::Index::enum_index_algorithm type)
{
  switch (type)
  {
  case dd::Index::IA_SE_SPECIFIC:
    return HA_KEY_ALG_SE_SPECIFIC;

  case dd::Index::IA_BTREE:
    return HA_KEY_ALG_BTREE;

  case dd::Index::IA_RTREE:
    return HA_KEY_ALG_RTREE;

  case dd::Index::IA_HASH:
    return HA_KEY_ALG_HASH;

  case dd::Index::IA_FULLTEXT:
    return HA_KEY_ALG_FULLTEXT;

  default:
    DBUG_ASSERT(!"Should not hit here"); /* purecov: deadcode */
  }

  return HA_KEY_ALG_SE_SPECIFIC;
}


/*
  Check if the given key_part is suitable to be promoted as part of
  primary key.
*/
bool is_suitable_for_primary_key(KEY_PART_INFO *key_part,
                                 Field *table_field)
{
  // Index on virtual generated columns is not allowed to be PK
  // even when the conditions below are true, so this case must be
  // rejected here.
  if (table_field->is_virtual_gcol())
    return false;

  /*
    If the key column is of NOT NULL BLOB type, then it
    will definitly have key prefix. And if key part prefix size
    is equal to the BLOB column max size, then we can promote
    it to primary key.
   */
  if (!table_field->real_maybe_null() &&
      table_field->type() == MYSQL_TYPE_BLOB &&
      table_field->field_length == key_part->length)
    return true;

  /*
    If the key column is of NOT NULL GEOMETRY type, specifically POINT
    type whose length is known internally (which is 25). And key part
    prefix size is equal to the POINT column max size, then we can
    promote it to primary key.
   */
  if (!table_field->real_maybe_null() &&
      table_field->type() == MYSQL_TYPE_GEOMETRY &&
      table_field->get_geometry_type() == Field::GEOM_POINT &&
      key_part->length == MAX_LEN_GEOM_POINT_FIELD)
    return true;

  if (table_field->real_maybe_null() ||
      table_field->key_length() != key_part->length)
    return false;

  return true;
}

/**
  Prepare TABLE_SHARE from dd::Table object or by reading metadata
  from dd.tables.

  This code similar to code in open_binary_frm(). Can be re-written
  independent to other efforts later.
*/

static bool prepare_share(THD *thd, TABLE_SHARE *share)
{
  my_bitmap_map *bitmaps;
  bool use_hash;
  handler *handler_file= 0;

  // Mark 'system' tables (tables with one row) to help the Optimizer.
  share->system= ((share->max_rows == 1) &&
                  (share->min_rows == 1) &&
                  (share->keys == 0));

  bool use_extended_sk=
    ha_check_storage_engine_flag(share->db_type(),
                                 HTON_SUPPORTS_EXTENDED_KEYS);
  // Setup name_hash for quick look-up
  use_hash= share->fields >= MAX_FIELDS_BEFORE_HASH;
  if (use_hash)
  {
    Field **field_ptr= share->field;
    use_hash= !my_hash_init(&share->name_hash,
                            system_charset_info, share->fields, 0,
                            get_field_name, nullptr, 0,
                            PSI_INSTRUMENT_ME);

    for (uint i=0 ; i < share->fields; i++, field_ptr++)
    {
        if (my_hash_insert(&share->name_hash, (uchar*) field_ptr) )
        {
          // OOM error message already reported
          return true; /* purecov: inspected */
        }
    }
  }


  // Setup other fields =====================================================
  /* Allocate handler */
  if (!(handler_file= get_new_handler(share, (share->m_part_info != NULL),
                                      &share->mem_root, share->db_type())))
  {
    my_error(ER_INVALID_DD_OBJECT, MYF(0),  share->path.str,
             "Failed to initialize handler.");
    return true;
  }

  if (handler_file->set_ha_share_ref(&share->ha_share))
  {
    my_error(ER_INVALID_DD_OBJECT, MYF(0),  share->path.str, "");
    return true;
  }
  share->db_low_byte_first= handler_file->low_byte_first();

  /* Fix key->name and key_part->field */
  if (share->keys)
  {
      KEY *keyinfo;
      KEY_PART_INFO *key_part;
      uint primary_key=(uint) (find_type(primary_key_name, &share->keynames,
                  FIND_TYPE_NO_PREFIX) - 1);
      longlong ha_option= handler_file->ha_table_flags();
      keyinfo= share->key_info;
      key_part= keyinfo->key_part;

      for (uint key=0 ; key < share->keys ; key++,keyinfo++)
      {
          uint usable_parts= 0;
          keyinfo->name=(char*) share->keynames.type_names[key];

          /* Check that fulltext and spatial keys have correct algorithm set. */
          DBUG_ASSERT(!(share->key_info[key].flags & HA_FULLTEXT) ||
                      share->key_info[key].algorithm == HA_KEY_ALG_FULLTEXT);
          DBUG_ASSERT(!(share->key_info[key].flags & HA_SPATIAL) ||
                      share->key_info[key].algorithm == HA_KEY_ALG_RTREE);

          if (primary_key >= MAX_KEY && (keyinfo->flags & HA_NOSAME))
          {
            /*
               If the UNIQUE key doesn't have NULL columns and is not a part key
               declare this as a primary key.
            */
            primary_key=key;
            for (uint i=0 ; i < keyinfo->user_defined_key_parts ;i++)
            {
              Field *table_field= key_part[i].field;

              if (is_suitable_for_primary_key(&key_part[i],
                                              table_field) == false)
              {
                primary_key= MAX_KEY;
                break;
              }
            }
          }

          for (uint i=0 ; i < keyinfo->user_defined_key_parts ; key_part++,i++)
          {
              Field *field= key_part->field;

              key_part->type= field->key_type();
              if (field->real_maybe_null())
              {
                  key_part->null_offset=field->null_offset(share->default_values);
                  key_part->null_bit= field->null_bit;
                  key_part->store_length+=HA_KEY_NULL_LENGTH;
                  keyinfo->flags|=HA_NULL_PART_KEY;
                  keyinfo->key_length+= HA_KEY_NULL_LENGTH;
              }
              if (field->type() == MYSQL_TYPE_BLOB ||
                      field->real_type() == MYSQL_TYPE_VARCHAR ||
                      field->type() == MYSQL_TYPE_GEOMETRY)
              {
                  key_part->store_length+=HA_KEY_BLOB_LENGTH;
                  if (i + 1 <= keyinfo->user_defined_key_parts)
                      keyinfo->key_length+= HA_KEY_BLOB_LENGTH;
              }
              key_part->init_flags();

              if (field->is_virtual_gcol())
                keyinfo->flags|= HA_VIRTUAL_GEN_KEY;

              setup_key_part_field(share, handler_file, primary_key,
                                   keyinfo, key, i, &usable_parts, true);

              field->flags|= PART_KEY_FLAG;
              if (key == primary_key)
              {
                  field->flags|= PRI_KEY_FLAG;
                  /*
                     If this field is part of the primary key and all keys contains
                     the primary key, then we can use any key to find this column
                     */
                  if (ha_option & HA_PRIMARY_KEY_IN_READ_INDEX)
                  {
                      if (field->key_length() == key_part->length &&
                              !(field->flags & BLOB_FLAG))
                          field->part_of_key= share->keys_in_use;
                      if (field->part_of_sortkey.is_set(key))
                          field->part_of_sortkey= share->keys_in_use;
                  }
              }
              if (field->key_length() != key_part->length)
              {
#ifndef TO_BE_DELETED_ON_PRODUCTION
                  if (field->type() == MYSQL_TYPE_NEWDECIMAL)
                  {
                      /*
                         Fix a fatal error in decimal key handling that causes crashes
                         on Innodb. We fix it by reducing the key length so that
                         InnoDB never gets a too big key when searching.
                         This allows the end user to do an ALTER TABLE to fix the
                         error.
                         */
                      keyinfo->key_length-= (key_part->length - field->key_length());
                      key_part->store_length-= (uint16)(key_part->length -
                              field->key_length());
                      key_part->length= (uint16)field->key_length();
                      sql_print_error("Found wrong key definition in %s; "
                              "Please do \"ALTER TABLE `%s` FORCE \" to fix it!",
                              share->table_name.str,
                              share->table_name.str);
                      push_warning_printf(thd, Sql_condition::SL_WARNING,
                              ER_CRASHED_ON_USAGE,
                              "Found wrong key definition in %s; "
                              "Please do \"ALTER TABLE `%s` FORCE\" to fix "
                              "it!",
                              share->table_name.str,
                              share->table_name.str);
                      share->crashed= 1;                // Marker for CHECK TABLE
                      continue;
                  }
#endif
                  key_part->key_part_flag|= HA_PART_KEY_SEG;
              }
          }

          /*
            KEY::flags is fully set-up at this point so we can copy it to
            KEY::actual_flags.
          */
          keyinfo->actual_flags= keyinfo->flags;

          if (use_extended_sk && primary_key < MAX_KEY &&
              key && !(keyinfo->flags & HA_NOSAME))
            key_part+= add_pk_parts_to_sk(keyinfo, key, share->key_info, primary_key,
                                      share,  handler_file, &usable_parts);

          /* Skip unused key parts if they exist */
          key_part+= keyinfo->unused_key_parts;

          keyinfo->usable_key_parts= usable_parts; // Filesort

          set_if_bigger(share->max_key_length,keyinfo->key_length+
                  keyinfo->user_defined_key_parts);
          share->total_key_length+= keyinfo->key_length;
          /*
             MERGE tables do not have unique indexes. But every key could be
             an unique index on the underlying MyISAM table. (Bug #10400)
             */
          if ((keyinfo->flags & HA_NOSAME) ||
                  (ha_option & HA_ANY_INDEX_MAY_BE_UNIQUE))
              set_if_bigger(share->max_unique_length,keyinfo->key_length);
      }
      if (primary_key < MAX_KEY &&
              (share->keys_in_use.is_set(primary_key)))
      {
          share->primary_key= primary_key;
          /*
             If we are using an integer as the primary key then allow the user to
             refer to it as '_rowid'
             */
          if (share->key_info[primary_key].user_defined_key_parts == 1)
          {
              Field *field= share->key_info[primary_key].key_part[0].field;
              if (field && field->result_type() == INT_RESULT)
              {
                  /* note that fieldnr here (and rowid_field_offset) starts from 1 */
                  share->rowid_field_offset= (share->key_info[primary_key].key_part[0].
                          fieldnr);
              }
          }
      }
      else
          share->primary_key = MAX_KEY; // we do not have a primary key
  }
  else
      share->primary_key= MAX_KEY;
  delete handler_file;

  if (share->found_next_number_field)
  {
      Field *reg_field= *share->found_next_number_field;
      if ((int) (share->next_number_index= (uint)
                  find_ref_key(share->key_info, share->keys,
                      share->default_values, reg_field,
                      &share->next_number_key_offset,
                      &share->next_number_keypart)) < 0)
      {
        my_error(ER_INVALID_DD_OBJECT, MYF(0),  share->path.str,
                 "Wrong field definition.");
        return true;
      }
      else
          reg_field->flags |= AUTO_INCREMENT_FLAG;
  }

  if (share->blob_fields)
  {
      Field **ptr;
      uint k, *save;

      /* Store offsets to blob fields to find them fast */
      if (!(share->blob_field= save=
                  (uint*) alloc_root(&share->mem_root,
                      (uint) (share->blob_fields* sizeof(uint)))))
        return true; // OOM error message already reported
      for (k=0, ptr= share->field ; *ptr ; ptr++, k++)
      {
          if ((*ptr)->flags & BLOB_FLAG)
              (*save++)= k;
      }
  }

  share->column_bitmap_size= bitmap_buffer_size(share->fields);
  if (!(bitmaps= (my_bitmap_map*) alloc_root(&share->mem_root,
                  share->column_bitmap_size)))
  {
    // OOM error message already reported
    return true; /* purecov: inspected */
  }
  bitmap_init(&share->all_set, bitmaps, share->fields, FALSE);
  bitmap_set_all(&share->all_set);

  return false;
}


/** Fill tablespace name from dd::Tablespace. */
static bool fill_tablespace_from_dd(THD *thd, TABLE_SHARE *share,
                                    const dd::Table *tab_obj)
{
  DBUG_ENTER("fill_tablespace_from_dd");

  DBUG_RETURN(dd::get_tablespace_name<dd::Table>(
                thd,
                tab_obj,
                const_cast<const char**>(&share->tablespace),
                &share->mem_root));
}


/**
  Convert row format value used in DD to corresponding value in old
  row_type enum.
*/

static row_type dd_get_old_row_format(dd::Table::enum_row_format new_format)
{
  switch (new_format)
  {
  case dd::Table::RF_FIXED:
    return ROW_TYPE_FIXED;
  case dd::Table::RF_DYNAMIC:
    return ROW_TYPE_DYNAMIC;
  case dd::Table::RF_COMPRESSED:
    return ROW_TYPE_COMPRESSED;
  case dd::Table::RF_REDUNDANT:
    return ROW_TYPE_REDUNDANT;
  case dd::Table::RF_COMPACT:
    return ROW_TYPE_COMPACT;
  case dd::Table::RF_PAGED:
    return ROW_TYPE_PAGED;
  default:
    DBUG_ASSERT(0);
    break;
  }
  return ROW_TYPE_FIXED;
}


/** Fill TABLE_SHARE from dd::Table object */
static bool fill_share_from_dd(THD *thd, TABLE_SHARE *share, const dd::Table *tab_obj)
{
  // Read table engine type
  plugin_ref tmp_plugin=
    ha_resolve_by_name_raw(thd, lex_cstring_handle(tab_obj->engine()));
  if (tmp_plugin)
  {
#ifndef DBUG_OFF
    handlerton *hton= plugin_data<handlerton *>(tmp_plugin);
#endif

    DBUG_ASSERT(hton && ha_storage_engine_is_enabled(hton));
    DBUG_ASSERT(!ha_check_storage_engine_flag(hton, HTON_NOT_USER_SELECTABLE));

    // For a partitioned table, the SE must support partitioning natively.
    DBUG_ASSERT(tab_obj->partition_type() == dd::Table::PT_NONE ||
                hton->partition_flags);

    plugin_unlock(NULL, share->db_plugin);
    share->db_plugin= my_plugin_lock(NULL, &tmp_plugin);
  }
  else
  {
    my_error(ER_UNKNOWN_STORAGE_ENGINE, MYF(0), tab_obj->engine().c_str());
    return true;
  }

  // Set temporarily a good value for db_low_byte_first.
  DBUG_ASSERT(ha_legacy_type(share->db_type()) != DB_TYPE_ISAM);
  share->db_low_byte_first= true;

  // Read other table options
  dd::Properties *table_options= const_cast<dd::Properties*>
    (&tab_obj->options());

  uint64 option_value;
  bool bool_opt;

  // Max rows
  if (table_options->exists("max_rows"))
    table_options->get_uint64("max_rows", &share->max_rows);

  // Min rows
  if (table_options->exists("min_rows"))
    table_options->get_uint64("min_rows", &share->min_rows);

  // Options from HA_CREATE_INFO::table_options/TABLE_SHARE::db_create_options.
  share->db_create_options= 0;

  table_options->get_bool("pack_record", &bool_opt);
  if (bool_opt)
    share->db_create_options|= HA_OPTION_PACK_RECORD;

  if (table_options->exists("pack_keys"))
  {
    table_options->get_bool("pack_keys", &bool_opt);
    share->db_create_options|= bool_opt ? HA_OPTION_PACK_KEYS :
                                            HA_OPTION_NO_PACK_KEYS;
  }

  if (table_options->exists("checksum"))
  {
    table_options->get_bool("checksum", &bool_opt);
    if (bool_opt)
      share->db_create_options|= HA_OPTION_CHECKSUM;
  }

  if (table_options->exists("delay_key_write"))
  {
    table_options->get_bool("delay_key_write", &bool_opt);
    if (bool_opt)
      share->db_create_options|= HA_OPTION_DELAY_KEY_WRITE;
  }

  if (table_options->exists("stats_persistent"))
  {
    table_options->get_bool("stats_persistent", &bool_opt);
    share->db_create_options|= bool_opt ? HA_OPTION_STATS_PERSISTENT :
                                            HA_OPTION_NO_STATS_PERSISTENT;
  }

  share->db_options_in_use= share->db_create_options;

  // Average row length

  if (table_options->exists("avg_row_length"))
  {
    table_options->get_uint64("avg_row_length", &option_value);
    share->avg_row_length= static_cast<ulong>(option_value);
  }

  // Collation ID
  share->table_charset= dd_get_mysql_charset(tab_obj->collation_id());
  if (!share->table_charset)
  {
    // Unknown collation
    if (use_mb(default_charset_info))
    {
      /* Warn that we may be changing the size of character columns */
      sql_print_warning("'%s' had no or invalid character set, "
                        "and default character set is multi-byte, "
                        "so character column sizes may have changed",
                        share->path.str);
    }
    share->table_charset= default_charset_info;
  }
  share->db_record_offset= 1;

  // Row type. First one really used by the storage engine.
  share->real_row_type= dd_get_old_row_format(tab_obj->row_format());

  // Then one which was explicitly specified by user for this table.
  if (table_options->exists("row_type"))
  {
    table_options->get_uint64("row_type", &option_value);
    share->row_type=
      dd_get_old_row_format((dd::Table::enum_row_format)option_value);
  }
  else
    share->row_type= ROW_TYPE_DEFAULT;

  // Stats_sample_pages
  if (table_options->exists("stats_sample_pages"))
    table_options->get_uint32("stats_sample_pages",
                              &share->stats_sample_pages);

  // Stats_auto_recalc
  if (table_options->exists("stats_auto_recalc"))
  {
    table_options->get_uint64("stats_auto_recalc", &option_value);
    share->stats_auto_recalc= (enum_stats_auto_recalc) option_value;
  }

  // mysql version
  share->mysql_version= tab_obj->mysql_version_id();

  // TODO-POST-MERGE-TO-TRUNK: Initialize new field
  // share->last_checked_for_upgrade? Or access tab_obj directly where
  // needed?

  // key block size
  table_options->get_uint32("key_block_size", &share->key_block_size);

  // Prepare the default_value buffer.
  if (prepare_default_value_buffer_and_table_share(thd, *tab_obj, share))
    return true;

  // Storage media flags
  if (table_options->exists("storage"))
  {
    uint32 option_value;
    table_options->get_uint32("storage", &option_value);
    share->default_storage_media=
      static_cast<ha_storage_media>(option_value);
  }
  else
    share->default_storage_media= HA_SM_DEFAULT;

  // Read tablespace name
  if (fill_tablespace_from_dd(thd, share, tab_obj))
    return true;

  // Read comment
  dd::String_type comment= tab_obj->comment();
  if (comment.length())
  {
    share->comment.str= strmake_root(&share->mem_root,
                                       comment.c_str(),
                                       comment.length()+1);
    share->comment.length= comment.length();
  }

  // Read Connection strings
  if (table_options->exists("connection_string"))
    table_options->get("connection_string",
                       share->connect_string,
                       &share->mem_root);

  // Read Compress string
  if (table_options->exists("compress"))
    table_options->get("compress", share->compress, &share->mem_root);

  // Read Encrypt string
  if (table_options->exists("encrypt_type"))
    table_options->get("encrypt_type", share->encrypt_type, &share->mem_root);

  return false;
}


/**
  Calculate number of bits used for the column in the record preamble
  (aka null bits number).
*/

static uint column_preamble_bits(const dd::Column *col_obj)
{
  uint result= 0;

  if (col_obj->is_nullable())
    result++;

  if (col_obj->type() == dd::enum_column_types::BIT)
  {
    bool treat_bit_as_char;
    (void) col_obj->options().get_bool("treat_bit_as_char",
                                       &treat_bit_as_char);

    if (! treat_bit_as_char)
      result+= col_obj->char_length() & 7;
  }
  return result;
}


/**
  Add Field constructed according to column metadata from dd::Column
  object to TABLE_SHARE.
*/

static bool fill_column_from_dd(TABLE_SHARE *share,
                                const dd::Column *col_obj,
                                uchar *null_pos,
                                uint null_bit_pos,
                                uchar *rec_pos,
                                uint field_nr)
{
  char *name= NULL;
  uchar auto_flags;
  size_t field_length;
  enum_field_types field_type;
  const CHARSET_INFO *charset=NULL;
  Field::geometry_type geom_type= Field::GEOM_GEOMETRY;
  Field *reg_field;
  uint decimals;
  ha_storage_media field_storage;
  column_format_type field_column_format;

  //
  // Read column details from dd table
  //

  // Column name
  dd::String_type s= col_obj->name();
  DBUG_ASSERT(!s.empty());
  name= strmake_root(&share->mem_root, s.c_str(), s.length());
  name[s.length()]= '\0';

  dd::Properties *column_options= const_cast<dd::Properties*>
                                    (&col_obj->options());

  // Type
  field_type= dd_get_old_field_type(col_obj->type());

  // Char length
  field_length= col_obj->char_length();

  // Reconstruct auto_flags
  auto_flags= Field::NONE;

  /*
    The only value for DEFAULT and ON UPDATE options which we support
    at this point is CURRENT_TIMESTAMP.
  */
  if (! col_obj->default_option().empty())
    auto_flags|= Field::DEFAULT_NOW;
  if (! col_obj->update_option().empty())
    auto_flags|= Field::ON_UPDATE_NOW;

  if (col_obj->is_auto_increment())
    auto_flags|= Field::NEXT_NUMBER;

  /*
    Columns can't have AUTO_INCREMENT and DEFAULT/ON UPDATE CURRENT_TIMESTAMP at the
    same time.
  */
  DBUG_ASSERT(!((auto_flags & (Field::DEFAULT_NOW|Field::ON_UPDATE_NOW)) != 0 &&
                (auto_flags & Field::NEXT_NUMBER) != 0));

  bool treat_bit_as_char= false;
  if (field_type == MYSQL_TYPE_BIT)
    column_options->get_bool("treat_bit_as_char", &treat_bit_as_char);

  // Collation ID
  charset= dd_get_mysql_charset(col_obj->collation_id());
  if (charset == NULL)
  {
    my_printf_error(ER_UNKNOWN_COLLATION,
                    "invalid collation id %llu for table %s, column %s",
                    MYF(0), col_obj->collation_id(), share->table_name.str,
                    name);
    return true;
  }

  // Decimals
  if (field_type == MYSQL_TYPE_DECIMAL || field_type == MYSQL_TYPE_NEWDECIMAL)
  {
    DBUG_ASSERT(col_obj->is_numeric_scale_null() == false);
    decimals= col_obj->numeric_scale();
  }
  else if (field_type == MYSQL_TYPE_FLOAT || field_type == MYSQL_TYPE_DOUBLE)
  {
    decimals= col_obj->is_numeric_scale_null() ? NOT_FIXED_DEC :
                                                 col_obj->numeric_scale();
  }
  else
    decimals= 0;

  // Read geometry sub type
  if (field_type == MYSQL_TYPE_GEOMETRY)
  {
    uint32 sub_type;
    column_options->get_uint32("geom_type", &sub_type);
    geom_type= (Field::geometry_type) sub_type;
  }

  // Read values of storage media and column format options
  if (column_options->exists("storage"))
  {
    uint32 option_value;
    column_options->get_uint32("storage", &option_value);
    field_storage= static_cast<ha_storage_media>(option_value);
  }
  else
    field_storage= HA_SM_DEFAULT;

  if (column_options->exists("column_format"))
  {
    uint32 option_value;
    column_options->get_uint32("column_format", &option_value);
    field_column_format= static_cast<column_format_type>(option_value);
  }
  else
    field_column_format= COLUMN_FORMAT_TYPE_DEFAULT;

  // Read Interval TYPELIB
  TYPELIB *interval= NULL;

  if (field_type == MYSQL_TYPE_ENUM || field_type == MYSQL_TYPE_SET)
  {
    //
    // Allocate space for interval (column elements)
    //
    size_t interval_parts= col_obj->elements_count();

    interval= (TYPELIB*) alloc_root(&share->mem_root, sizeof(TYPELIB));
    interval->type_names=
      (const char **) alloc_root(&share->mem_root, sizeof(char*) * (interval_parts+1));
    interval->type_names[interval_parts]= 0;

    interval->type_lengths=
      (uint *) alloc_root(&share->mem_root, sizeof(uint) * interval_parts);
    interval->count= interval_parts;
    interval->name= NULL;

    //
    // Iterate through all the column elements
    //
    for (const dd::Column_type_element *ce : col_obj->elements())
    {
      // Read the enum/set element name
      dd::String_type element_name= ce->name();

      uint pos= ce->index() - 1;
      interval->type_lengths[pos]= static_cast<uint>(element_name.length());
      interval->type_names[pos]= strmake_root(
                                   &share->mem_root,
                                   element_name.c_str(),
                                   element_name.length());
    }
  }

  // Handle generated columns;
  Generated_column *gcol_info= NULL;
  if (!col_obj->is_generation_expression_null())
  {
    gcol_info= new (&share->mem_root) Generated_column();

    // Is GC virtual or stored ?
    gcol_info->set_field_stored(!col_obj->is_virtual());

    // Read generation expression.
    dd::String_type gc_expr= col_obj->generation_expression();

    /*
      Place the expression's text into the TABLE_SHARE. Field objects of
      TABLE_SHARE only have that. They don't have a corresponding Item,
      which will be later created for the Field in TABLE, by
      fill_dd_columns_from_create_fields().
    */
    gcol_info->dup_expr_str(&share->mem_root,
                            gc_expr.c_str(),
                            gc_expr.length());
    share->vfields++;
  }

  //
  // Create FIELD
  //
  reg_field= make_field(share, rec_pos,
                        (uint32) field_length,
                        null_pos, null_bit_pos,
                        field_type,
                        charset,
                        geom_type,
                        auto_flags,
                        interval,
                        name,
                        col_obj->is_nullable(),
                        col_obj->is_zerofill(),
                        col_obj->is_unsigned(),
                        decimals, treat_bit_as_char, 0);

  reg_field->field_index= field_nr;
  reg_field->gcol_info= gcol_info;
  reg_field->stored_in_db= gcol_info ? gcol_info->get_field_stored() : true;

  if (auto_flags & Field::NEXT_NUMBER)
    share->found_next_number_field= &share->field[field_nr];

  // Set field flags
  if (col_obj->has_no_default())
    reg_field->flags|= NO_DEFAULT_VALUE_FLAG;

  // Set default value or NULL. Reset required for e.g. CHAR.
  if (col_obj->is_default_value_null())
  {
    reg_field->reset();
    reg_field->set_null();
  }
  else if (field_type == MYSQL_TYPE_BIT  && !treat_bit_as_char &&
           (col_obj->char_length() & 7))
  {
    // For bit fields with leftover bits, copy leftover bits into the preamble.
    Field_bit *bitfield= dynamic_cast<Field_bit*>(reg_field);
    const uchar leftover_bits= static_cast<uchar>(*col_obj->default_value().
      substr(reg_field->pack_length() - 1, 1).data());
    set_rec_bits(leftover_bits, bitfield->bit_ptr, bitfield->bit_ofs,
                 bitfield->bit_len);
    // Copy the main part of the bit field data into the record body.
    memcpy(rec_pos, col_obj->default_value().data(),
           reg_field->pack_length() - 1);
  }
  else
  {
    // For any other field with default data, copy the data into the record.
    memcpy(rec_pos, col_obj->default_value().data(),
           reg_field->pack_length());
  }

  reg_field->set_storage_type(field_storage);
  reg_field->set_column_format(field_column_format);

  // Comments
  dd::String_type comment= col_obj->comment();
  reg_field->comment.length= comment.length();
  if (reg_field->comment.length)
  {
    reg_field->comment.str= strmake_root(&share->mem_root,
                                         comment.c_str(),
                                         comment.length());
    reg_field->comment.length= comment.length();
  }

  // Field is prepared. Store it in 'share'
  share->field[field_nr]= reg_field;

  return (false);
}


/**
  Populate TABLE_SHARE::field array according to column metadata
  from dd::Table object.
*/

static bool fill_columns_from_dd(TABLE_SHARE *share, const dd::Table *tab_obj)
{
  // Allocate space for fields in TABLE_SHARE.
  uint fields_size= ((share->fields+1)*sizeof(Field*));
  share->field= (Field **) alloc_root(&share->mem_root,
                                        (uint) fields_size);
  memset(share->field, 0, fields_size);
  share->vfields= 0;

  // Iterate through all the columns.
  uchar *null_flags MY_ATTRIBUTE((unused));
  uchar *null_pos, *rec_pos;
  null_flags= null_pos= share->default_values;
  rec_pos= share->default_values + share->null_bytes;
  uint null_bit_pos= (share->db_create_options & HA_OPTION_PACK_RECORD) ? 0 : 1;
  uint field_nr= 0;
  bool has_vgc= false;
  for (const dd::Column *col_obj : tab_obj->columns())
  {
    // Skip hidden columns
    if (col_obj->is_hidden())
      continue;

    /*
      Fill details of each column.

      Skip virtual generated columns at this point. They reside at the end of
      the record, so we need to do separate pass, to evaluate their offsets
      correctly.
    */
    if (!col_obj->is_virtual())
    {
      if (fill_column_from_dd(share, col_obj, null_pos, null_bit_pos,
                              rec_pos, field_nr))
        return true;

      rec_pos+= share->field[field_nr]->pack_length_in_rec();
    }
    else
      has_vgc= true;

    /*
      Virtual generated columns still need to be accounted in null bits and
      field_nr calculations, since they reside at the normal place in record
      preamble and TABLE_SHARE::field array.
    */
    if ((null_bit_pos+= column_preamble_bits(col_obj)) > 7)
    {
      null_pos++;
      null_bit_pos-= 8;
    }
    field_nr++;
  }

  if (has_vgc)
  {
    /*
      Additional pass to put virtual generated columns at the end of the
      record is required.
    */
    if (share->stored_rec_length > static_cast<ulong>(rec_pos - share->default_values))
      share->stored_rec_length= (rec_pos - share->default_values);

    null_pos= share->default_values;
    null_bit_pos= (share->db_create_options & HA_OPTION_PACK_RECORD) ? 0 : 1;
    field_nr= 0;

    for (const dd::Column *col_obj2 : tab_obj->columns())
    {
      // Skip hidden columns
      if (col_obj2->is_hidden())
        continue;

      if (col_obj2->is_virtual())
      {
        // Fill details of each column.
        if (fill_column_from_dd(share, col_obj2, null_pos, null_bit_pos,
                                rec_pos, field_nr))
          return true;

        rec_pos+= share->field[field_nr]->pack_length_in_rec();
      }

      /*
        Account for all columns while evaluating null_pos/null_bit_pos and
        field_nr.
      */
      if ((null_bit_pos+= column_preamble_bits(col_obj2)) > 7)
      {
        null_pos++;
        null_bit_pos-= 8;
      }
      field_nr++;
    }
  }

  // Make sure the scan of the columns is consistent with other data.
  DBUG_ASSERT(share->null_bytes == (null_pos - null_flags +
                                      (null_bit_pos + 7) / 8));
  DBUG_ASSERT(share->last_null_bit_pos == null_bit_pos);
  DBUG_ASSERT(share->fields == field_nr);

  return (false);
}


/** Fill KEY_INFO_PART from dd::Index_element object. */
static void fill_index_element_from_dd(TABLE_SHARE *share,
                                       const dd::Index_element *idx_elem_obj,
                                       KEY_PART_INFO *keypart)
{
  //
  // Read index element details
  //

  keypart->length= idx_elem_obj->length();
  keypart->store_length= keypart->length;

  // fieldnr
  keypart->fieldnr= idx_elem_obj->column().ordinal_position();

  // field
  DBUG_ASSERT(keypart->fieldnr > 0);
  Field *field= keypart->field= share->field[keypart->fieldnr-1];

  // offset
  keypart->offset= field->offset(share->default_values);

  // key type
  keypart->bin_cmp= ((field->real_type() != MYSQL_TYPE_VARCHAR &&
                      field->real_type() != MYSQL_TYPE_STRING) ||
                     (field->charset()->state & MY_CS_BINSORT));
  //
  // Read index order
  //

  // key part order
  if (idx_elem_obj->order() == dd::Index_element::ORDER_DESC)
    keypart->key_part_flag|= HA_REVERSE_SORT;

  // key_part->field=   (Field*) 0; // Will be fixed later
}


/** Fill KEY::key_part array according to metadata from dd::Index object. */
static void fill_index_elements_from_dd(TABLE_SHARE *share,
                                        const dd::Index *idx_obj,
                                        int key_nr)
{
  //
  // Iterate through all index elements
  //

  uint i= 0;
  KEY *keyinfo= share->key_info + key_nr;
  for (const dd::Index_element *idx_elem_obj : idx_obj->elements())
  {
    // Skip hidden index elements
    if (idx_elem_obj->is_hidden())
      continue;

    //
    // Read index element details
    //

    fill_index_element_from_dd(share, idx_elem_obj, keyinfo->key_part + i);

    i++;
  }
}


/**
  Add KEY constructed according to index metadata from dd::Index object to
  the TABLE_SHARE.
*/

static bool fill_index_from_dd(TABLE_SHARE *share, const dd::Index *idx_obj,
                               uint key_nr)
{
  //
  // Read index details
  //

  // Get the keyinfo that we will prepare now
  KEY *keyinfo= share->key_info + key_nr;

  // Read index name
  const dd::String_type &name= idx_obj->name();
  if (!name.empty())
  {
    if (name.length())
    {
      keyinfo->name= strmake_root(&share->mem_root,
                                  name.c_str(),
                                  name.length());
      share->keynames.type_names[key_nr]= keyinfo->name; //Post processing ??
    }
    else
      share->keynames.type_names[key_nr]= NULL;
    //share->keynames.count= key_nr+1;
  }

  // Index algorithm
  keyinfo->algorithm= dd_get_old_index_algorithm_type(idx_obj->algorithm());
  keyinfo->is_algorithm_explicit= idx_obj->is_algorithm_explicit();

  // Visibility
  keyinfo->is_visible= idx_obj->is_visible();

  // user defined key parts
  keyinfo->user_defined_key_parts= 0;
  for (const dd::Index_element *idx_ele : idx_obj->elements())
  {
    // Skip hidden index elements
    if (!idx_ele->is_hidden())
      keyinfo->user_defined_key_parts++;
  }

  // flags
  switch (idx_obj->type()) {
  case dd::Index::IT_MULTIPLE:
    keyinfo->flags= 0;
    break;
  case dd::Index::IT_FULLTEXT:
    keyinfo->flags= HA_FULLTEXT;
    break;
  case dd::Index::IT_SPATIAL:
    keyinfo->flags= HA_SPATIAL;
    break;
  case dd::Index::IT_PRIMARY:
  case dd::Index::IT_UNIQUE:
    keyinfo->flags= HA_NOSAME;
    break;
  default:
    DBUG_ASSERT(0); /* purecov: deadcode */
    keyinfo->flags= 0;
    break;
  }

  if (idx_obj->is_generated())
    keyinfo->flags|= HA_GENERATED_KEY;

  /*
    The remaining important SQL-layer flags are set later - either we directly
    store and read them from DD (HA_PACK_KEY, HA_BINARY_PACK_KEY), or calculate
    while handling other key options (HA_USES_COMMENT, HA_USES_PARSER,
    HA_USES_BLOCK_SIZE), or during post-processing step (HA_NULL_PART_KEY).
  */

  // key length
  keyinfo->key_length= 0;
  for (const dd::Index_element *idx_elem : idx_obj->elements())
  {
    // Skip hidden index elements
    if (!idx_elem->is_hidden())
      keyinfo->key_length+= idx_elem->length();
  }

  //
  // Read index options
  //

  dd::Properties *idx_options= const_cast<dd::Properties*>(&idx_obj->options());

  /*
    Restore flags indicating that key packing optimization was suggested to SE.
    See fill_dd_indexes_for_keyinfo() for explanation why we store these flags
    explicitly.
  */
  uint32 stored_flags;
  idx_options->get_uint32("flags", &stored_flags);
  DBUG_ASSERT((stored_flags & ~(HA_PACK_KEY | HA_BINARY_PACK_KEY)) == 0);
  keyinfo->flags|= stored_flags;

  // Block size
  if (idx_options->exists("block_size"))
  {
    idx_options->get_uint32("block_size", &keyinfo->block_size);

    DBUG_ASSERT(keyinfo->block_size);

    keyinfo->flags|= HA_USES_BLOCK_SIZE;
  }

  // Read field parser
  if (idx_options->exists("parser_name"))
  {
    LEX_CSTRING parser_name;
    dd::String_type pn= idx_options->value_cstr("parser_name");

    DBUG_ASSERT(!pn.empty());

    parser_name.str= (char*) strmake_root(&share->mem_root,
                                          pn.c_str(),
                                          pn.length());
    parser_name.length= pn.length();

    keyinfo->parser= my_plugin_lock_by_name(NULL, parser_name,
                                            MYSQL_FTPARSER_PLUGIN);
    if (! keyinfo->parser)
    {
      my_error(ER_PLUGIN_IS_NOT_LOADED, MYF(0), parser_name.str);
      return true;
    }

    keyinfo->flags|= HA_USES_PARSER;
  }

  // Read comment
  dd::String_type comment= idx_obj->comment();
  keyinfo->comment.length= comment.length();

  if (keyinfo->comment.length)
  {
    keyinfo->comment.str= strmake_root(&share->mem_root,
                                       comment.c_str(),
                                       comment.length());
    keyinfo->comment.length= comment.length();

    keyinfo->flags|= HA_USES_COMMENT;
  }

  return (false);
}


/**
  Fill TABLE_SHARE::key_info array according to index metadata
  from dd::Table object.
*/

static bool fill_indexes_from_dd(TABLE_SHARE *share, const dd::Table *tab_obj)
{
  uint32 primary_key_parts= 0;

  bool use_extended_sk=
    ha_check_storage_engine_flag(share->db_type(),
                                 HTON_SUPPORTS_EXTENDED_KEYS);

  // Count number of keys and total number of key parts in the table.

  DBUG_ASSERT(share->keys == 0 && share->key_parts == 0);

  for (const dd::Index *idx_obj : tab_obj->indexes())
  {
    // Skip hidden indexes
    if (idx_obj->is_hidden())
      continue;

    share->keys++;
    uint key_parts= 0;
    for (const dd::Index_element *idx_ele : idx_obj->elements())
    {
      // Skip hidden index elements
      if (!idx_ele->is_hidden())
        key_parts++;
    }
    share->key_parts+= key_parts;

    // Primary key (or candidate key replacing it) is always first if exists.
    // If such key doesn't exist (e.g. there are no unique keys in the table)
    // we will simply waste some memory.
    if (idx_obj->ordinal_position() == 1)
      primary_key_parts= key_parts;
  }

  share->keys_for_keyread.init(0);
  share->keys_in_use.init();
  share->visible_indexes.init();

  // Allocate and fill KEY objects.
  if (share->keys)
  {
    KEY_PART_INFO *key_part;
    ulong *rec_per_key;
    rec_per_key_t *rec_per_key_float;
    uint total_key_parts= share->key_parts;

    if (use_extended_sk)
        total_key_parts+= (primary_key_parts * (share->keys-1));

    //
    // Alloc rec_per_key buffer
    //
    if (!(rec_per_key= (ulong*) alloc_root(&share->mem_root,
                                         total_key_parts *
                                         sizeof(ulong) )))
      return true; /* purecov: inspected */

    //
    // Alloc rec_per_key_float buffer
    //
    if (!(rec_per_key_float= (rec_per_key_t *) alloc_root(
                                                 &share->mem_root,
                                                 total_key_parts *
                                                 sizeof(rec_per_key_t) )))
      return true; /* purecov: inspected */

    //
    // Alloc buffer to hold keys and key_parts
    //

    if (!(share->key_info = (KEY*) alloc_root(&share->mem_root,
                                                share->keys * sizeof(KEY) +
                                                total_key_parts *
                                                sizeof(KEY_PART_INFO))))
      return true; /* purecov: inspected */

    memset(share->key_info, 0, (share->keys*sizeof(KEY) +
                                total_key_parts * sizeof(KEY_PART_INFO)));
    key_part= (KEY_PART_INFO*)(share->key_info+share->keys);

    //
    // Alloc buffer to hold keynames
    //

    if (!(share->keynames.type_names=
            (const char **) alloc_root(&share->mem_root,
                                 (share->keys+1) * sizeof(char*))))
      return true; /* purecov: inspected */
    memset(share->keynames.type_names, 0, ((share->keys+1)*sizeof(char*)));

    share->keynames.type_names[share->keys]= NULL;
    share->keynames.count= share->keys;

    // In first iteration get all the index_obj, so that we get all
    // user_defined_key_parts for each key. This is required to propertly
    // allocation key_part memory for keys.
    const dd::Index *index_at_pos[MAX_INDEXES];
    uint key_nr= 0;
    for (const dd::Index *idx_obj : tab_obj->indexes())
    {
      // Skip hidden indexes
      if (idx_obj->is_hidden())
        continue;

      if (fill_index_from_dd(share, idx_obj, key_nr))
        return true;

      index_at_pos[key_nr]= idx_obj;

      share->keys_in_use.set_bit(key_nr);
      if (idx_obj->is_visible())
        share->visible_indexes.set_bit(key_nr);

      key_nr++;
    }

    // Update keyparts now
    key_nr= 0;
    do
    {
      // Assign the key_part_info buffer
      KEY* keyinfo= &share->key_info[key_nr];
      keyinfo->key_part= key_part;
      keyinfo->set_rec_per_key_array(rec_per_key, rec_per_key_float);
      keyinfo->set_in_memory_estimate(IN_MEMORY_ESTIMATE_UNKNOWN);

      fill_index_elements_from_dd(share,
                                  index_at_pos[key_nr],
                                  key_nr);

      key_part+=keyinfo->user_defined_key_parts;
      rec_per_key+=keyinfo->user_defined_key_parts;
      rec_per_key_float+= keyinfo->user_defined_key_parts;

      // Post processing code ?
      /*
        Add PK parts if engine supports PK extension for secondary keys.
        Atm it works for Innodb only. Here we add unique first key parts
        to the end of secondary key parts array and increase actual number
        of key parts. Note that primary key is always first if exists.
        Later if there is no PK in the table then number of actual keys parts
        is set to user defined key parts.
        KEY::actual_flags can't be set until we fully set-up KEY::flags.
      */
      keyinfo->actual_key_parts= keyinfo->user_defined_key_parts;
      if (use_extended_sk && key_nr && !(keyinfo->flags & HA_NOSAME))
      {
        keyinfo->unused_key_parts= primary_key_parts;
        key_part+= primary_key_parts;
        rec_per_key+= primary_key_parts;
        rec_per_key_float+= primary_key_parts;
        share->key_parts+= primary_key_parts;
      }

      // Initialize the rec per key arrays
      for (uint kp= 0; kp < keyinfo->actual_key_parts; ++kp)
      { 
        keyinfo->rec_per_key[kp]= 0;
        keyinfo->set_records_per_key(kp, REC_PER_KEY_UNKNOWN);
      }

      key_nr++;
    } while (key_nr < share->keys);
  }

  return (false);
}


static char *copy_option_string(MEM_ROOT *mem_root,
                                const dd::Properties &options,
                                const dd::String_type &key)
{
  dd::String_type tmp_str;
  options.get(key, tmp_str);
  if (tmp_str.length())
  {
    return strdup_root(mem_root, tmp_str.c_str());
  }
  return NULL;
}


static void get_partition_options(MEM_ROOT *mem_root,
                                  partition_element *part_elem,
                                  const dd::Properties &part_options)
{
  if (part_options.exists("max_rows"))
    part_options.get_uint64("max_rows", &part_elem->part_max_rows);

  if (part_options.exists("min_rows"))
    part_options.get_uint64("min_rows", &part_elem->part_min_rows);

  part_elem->data_file_name= copy_option_string(mem_root,
                                                part_options,
                                                "data_file_name");
  part_elem->index_file_name= copy_option_string(mem_root,
                                                 part_options,
                                                 "index_file_name");

  uint32 nodegroup_id= UNDEF_NODEGROUP;
  if (part_options.exists("nodegroup_id"))
    part_options.get_uint32("nodegroup_id", &nodegroup_id);

  DBUG_ASSERT(nodegroup_id <= 0xFFFF);
  part_elem->nodegroup_id= nodegroup_id;
}


static bool get_part_column_values(MEM_ROOT *mem_root,
                                   partition_info *part_info,
                                   partition_element *part_elem,
                                   const dd::Partition *part_obj)
{
  part_elem_value *p_elem_values, *p_val;
  part_column_list_val *col_val_array, *col_vals;
  uint list_index= 0, entries= 0;
  uint max_column_id= 0, max_list_index= 0;

  for (const dd::Partition_value *part_value : part_obj->values())
  {
    max_column_id= std::max(max_column_id, part_value->column_num());
    max_list_index= std::max(max_list_index, part_value->list_num());
    entries++;
  }
  if (entries != ((max_column_id + 1) * (max_list_index + 1)))
  {
    DBUG_ASSERT(0); /* purecov: deadcode */
    return true;
  }

  part_info->num_columns= max_column_id + 1;

  if (!multi_alloc_root(mem_root,
                        &p_elem_values,
                        sizeof(*p_elem_values) * (max_list_index + 1),
                        &col_val_array,
                        sizeof(*col_val_array) *
                          part_info->num_columns * (max_list_index + 1),
                        NULL))
  {
    return true; /* purecov: inspected */
  }
  memset(p_elem_values, 0, sizeof(*p_elem_values) * (max_list_index + 1));
  memset(col_val_array,
         0,
         sizeof(*col_val_array) *
           part_info->num_columns * (max_list_index + 1));
  for (list_index= 0; list_index <= max_list_index; list_index++)
  {
    p_val= &p_elem_values[list_index];
    p_val->added_items= 1;
    p_val->col_val_array= &col_val_array[list_index * part_info->num_columns];
  }

  for (const dd::Partition_value *part_value : part_obj->values())
  {
    p_val= &p_elem_values[part_value->list_num()];
    col_vals= p_val->col_val_array;
    if (part_value->is_value_null())
    {
      col_vals[part_value->column_num()].null_value= true;
    }
    else if (part_value->max_value())
    {
      col_vals[part_value->column_num()].max_value= true;
    }
    else
    {
      // TODO-PARTITION: Perhaps allocate on the heap instead and when the first
      // table instance is opened, free it and add the field image instead?
      // That way it can be reused for all other table instances.
      col_vals[part_value->column_num()].column_value.value_str=
        strmake_root(mem_root,
                     part_value->value_utf8().c_str(),
                     part_value->value_utf8().length());
    }
  }

  for (list_index= 0; list_index <= max_list_index; list_index++)
  {
    p_val= &p_elem_values[list_index];
#ifndef DBUG_OFF
    for (uint i= 0; i < part_info->num_columns; i++)
    {
      DBUG_ASSERT(p_val->col_val_array[i].null_value ||
                  p_val->col_val_array[i].max_value ||
                  p_val->col_val_array[i].column_value.value_str);
    }
#endif
    if (part_elem->list_val_list.push_back(p_val, mem_root))
      return true;
  }

  return false;
}


static bool setup_partition_from_dd(THD *thd,
                                    MEM_ROOT *mem_root,
                                    partition_info *part_info,
                                    partition_element *part_elem,
                                    const dd::Partition *part_obj,
                                    bool is_subpart)
{
  dd::String_type comment= part_obj->comment();
  if (comment.length())
  {
    part_elem->part_comment= strdup_root(mem_root, comment.c_str());
    if (!part_elem->part_comment)
      return true;
  }
  part_elem->partition_name= strdup_root(mem_root, part_obj->name().c_str());
  if (!part_elem->partition_name)
    return true;

  part_elem->engine_type= part_info->default_engine_type;

  get_partition_options(mem_root, part_elem, part_obj->options());

  // Read tablespace name.
  if (dd::get_tablespace_name<dd::Partition>(thd,
                                             part_obj,
                                             &part_elem->tablespace_name,
                                             mem_root))
    return true;

  if (is_subpart)
  {
    /* Only HASH/KEY subpartitioning allowed, no values allowed, so return! */
    return false;
  }
  // Iterate over all possible values
  if (part_info->part_type == partition_type::RANGE)
  {
    if (part_info->column_list)
    {
      if (get_part_column_values(mem_root, part_info, part_elem, part_obj))
        return true;
    }
    else
    {
      DBUG_ASSERT(part_obj->values().size() == 1);
      const dd::Partition_value *part_value= *part_obj->values().begin();
      DBUG_ASSERT(part_value->list_num() == 0);
      DBUG_ASSERT(part_value->column_num() == 0);
      if (part_value->max_value())
      {
        part_elem->max_value= true;
      }
      else
      {
        if (part_value->value_utf8()[0] == '-')
        {
          part_elem->signed_flag= true;
          if (dd::Properties::to_int64(part_value->value_utf8(),
                                       &part_elem->range_value))
          {
            return true;
          }
        }
        else
        {
          part_elem->signed_flag= false;
          if (dd::Properties::to_uint64(part_value->value_utf8(),
                                        (ulonglong*) &part_elem->range_value))
          {
            return true;
          }
        }
      }
    }
  }
  else if (part_info->part_type == partition_type::LIST)
  {
    if (part_info->column_list)
    {
      if (get_part_column_values(mem_root, part_info, part_elem, part_obj))
        return true;
    }
    else
    {
      uint list_index= 0, max_index= 0, entries= 0, null_entry= 0;
      part_elem_value *list_val, *list_val_array= NULL;
      for (const dd::Partition_value *part_value : part_obj->values())
      {
        max_index= std::max(max_index, part_value->list_num());
        entries++;
        if (part_value->value_utf8().empty())
        {
          DBUG_ASSERT(!part_elem->has_null_value);
          part_elem->has_null_value= true;
          null_entry= part_value->list_num();
        }
      }
      if (entries != (max_index + 1))
      {
        DBUG_ASSERT(0); /* purecov: deadcode */
        return true;
      }
      /* If a list entry is NULL then it is only flagged on the part_elem. */
      if (part_elem->has_null_value)
        entries--;

      if (entries)
      {
        list_val_array= (part_elem_value*) alloc_root(mem_root,
                                                      sizeof(*list_val_array) *
                                                        entries);
        if (!list_val_array)
          return true;
        memset(list_val_array, 0, sizeof(*list_val_array) * entries);
      }

      for (const dd::Partition_value *part_value : part_obj->values())
      {
        DBUG_ASSERT(part_value->column_num() == 0);
        if (part_value->value_utf8().empty())
        {
          DBUG_ASSERT(part_value->list_num() == null_entry);
          continue;
        }
        list_index= part_value->list_num();
        /*
          If there is a NULL value in the partition values in the DD it is
          marked directly on the partition_element and should not have an own
          list_val. So compact the list_index range by remove the list_index for
          the null_entry.
        */
        if (part_elem->has_null_value && list_index > null_entry)
          list_index--;
        list_val= &list_val_array[list_index];
        DBUG_ASSERT(!list_val->unsigned_flag &&
                    !list_val->value);
        if (part_value->value_utf8()[0] == '-')
        {
          list_val->unsigned_flag= false;
          if (dd::Properties::to_int64(part_value->value_utf8(),
                                       &list_val->value))
            return true;
        }
        else
        {
          list_val->unsigned_flag= true;
          if (dd::Properties::to_uint64(part_value->value_utf8(),
                                        (ulonglong*) &list_val->value))
            return true;
        }
      }
      for (uint i= 0; i < entries; i++)
      {
        if (part_elem->list_val_list.push_back(&list_val_array[i],
                                               mem_root))
          return true;
      }
    }
  }
  else
  {
#ifndef DBUG_OFF
    DBUG_ASSERT(part_info->part_type == partition_type::HASH);
    DBUG_ASSERT(part_obj->values().empty());
#endif
  }
  return false;
}


/**
  Set field_list

  To append each field to the field_list it will parse the
  submitted partition_expression string.

  Must be in sync with get_field_list_str!

  @param[in]     mem_root   Where to allocate the memory for the list entries.
  @param[in]     str        String object containing the column names.
  @param[in,out] field_list List to add field names to.

  @return false on success, else true.
*/

static bool set_field_list(MEM_ROOT *mem_root,
                           dd::String_type &str,
                           List<char> *field_list)
{
  dd::String_type field_name;
  dd::String_type::const_iterator it(str.begin());
  dd::String_type::const_iterator end(str.end());

  while (it != end)
  {
    if (dd::eat_str(field_name, it, end, dd::FIELD_NAME_SEPARATOR_CHAR))
      return true;
    size_t len= field_name.length();
    DBUG_ASSERT(len);
    char *name= static_cast<char*>(alloc_root(mem_root, len + 1));
    if (!name)
      return true; /* purecov: inspected */
    memcpy(name, field_name.c_str(), len);
    name[len]= '\0';

    if (field_list->push_back(name, mem_root))
      return true;
  }
  return false;
}


/**
  Fill TABLE_SHARE with partitioning details from dd::Partition.

  @details
  Set up as much as possible to ease creating new TABLE instances
  by copying from the TABLE_SHARE.

  Also to prevent future memory duplication partition definitions (names etc)
  are stored on the TABLE_SHARE and can be referenced from each TABLE instance.

  Note that [sub]part_expr still needs to be parsed from
  [sub]part_func_string for each TABLE instance to use the correct
  mem_root etc. To be as compatible with the .frm way to open a table
  as possible we currently generate the full partitioning clause which
  will be parsed for each new TABLE instance.
  TODO-PARTITION:
  - Create a way to handle Item expressions to be shared/copied
    from the TABLE_SHARE.
  - On the open of the first TABLE instance, copy the field images
    to the TABLE_SHARE::partition_info for each partition value.

  @param thd      Thread context.
  @param share    Share to be updated with partitioning details.
  @param tab_obj  dd::Table object to get partition info from.

  @return false if success, else true.
*/

static bool fill_partitioning_from_dd(THD *thd, TABLE_SHARE *share,
                                      const dd::Table *tab_obj)
{
  if (tab_obj->partition_type() == dd::Table::PT_NONE)
    return false;

  partition_info *part_info;
  part_info= new (&share->mem_root) partition_info;

  handlerton *hton=
    plugin_data<handlerton *>
    (ha_resolve_by_name_raw(thd,
                            lex_cstring_handle(tab_obj->engine())));
  DBUG_ASSERT(hton && ha_storage_engine_is_enabled(hton));
  part_info->default_engine_type= hton;
  if (!part_info->default_engine_type)
    return true;

  // TODO-PARTITION: change partition_info::part_type to same enum as below :)
  switch (tab_obj->partition_type()) {
  case dd::Table::PT_RANGE_COLUMNS:
    part_info->column_list= true;
    part_info->list_of_part_fields= true;
    // Fall through.
  case dd::Table::PT_RANGE:
    part_info->part_type= partition_type::RANGE;
    break;
  case dd::Table::PT_LIST_COLUMNS:
    part_info->column_list= true;
    part_info->list_of_part_fields= true;
    // Fall through.
  case dd::Table::PT_LIST:
    part_info->part_type= partition_type::LIST;
    break;
  case dd::Table::PT_LINEAR_HASH:
    part_info->linear_hash_ind= true;
    // Fall through.
  case dd::Table::PT_HASH:
    part_info->part_type= partition_type::HASH;
    break;
  case dd::Table::PT_LINEAR_KEY_51:
    part_info->linear_hash_ind= true;
    // Fall through.
  case dd::Table::PT_KEY_51:
    part_info->key_algorithm= enum_key_algorithm::KEY_ALGORITHM_51;
    part_info->list_of_part_fields= true;
    part_info->part_type= partition_type::HASH;
    break;
  case dd::Table::PT_LINEAR_KEY_55:
    part_info->linear_hash_ind= true;
    // Fall through.
  case dd::Table::PT_KEY_55:
    part_info->key_algorithm= enum_key_algorithm::KEY_ALGORITHM_55;
    part_info->list_of_part_fields= true;
    part_info->part_type= partition_type::HASH;
    break;
  case dd::Table::PT_AUTO_LINEAR:
    part_info->linear_hash_ind= true;
    // Fall through.
  case dd::Table::PT_AUTO:
    part_info->key_algorithm= enum_key_algorithm::KEY_ALGORITHM_55;
    part_info->part_type= partition_type::HASH;
    part_info->list_of_part_fields= TRUE;
    part_info->is_auto_partitioned= true;
    share->auto_partitioned= true;
    break;
  default:
    // Unknown partitioning type!
    DBUG_ASSERT(0); /* purecov: deadcode */
    return true;
  }
  switch (tab_obj->subpartition_type()) {
  case dd::Table::ST_NONE:
    part_info->subpart_type= partition_type::NONE;
    break;
  case dd::Table::ST_LINEAR_HASH:
    part_info->linear_hash_ind= true;
    // Fall through.
  case dd::Table::ST_HASH:
    part_info->subpart_type= partition_type::HASH;
    break;
  case dd::Table::ST_LINEAR_KEY_51:
    part_info->linear_hash_ind= true;
    // Fall through.
  case dd::Table::ST_KEY_51:
    part_info->key_algorithm= enum_key_algorithm::KEY_ALGORITHM_51;
    part_info->list_of_subpart_fields= true;
    part_info->subpart_type= partition_type::HASH;
    break;
  case dd::Table::ST_LINEAR_KEY_55:
    part_info->linear_hash_ind= true;
    // Fall through.
  case dd::Table::ST_KEY_55:
    part_info->key_algorithm= enum_key_algorithm::KEY_ALGORITHM_55;
    part_info->list_of_subpart_fields= true;
    part_info->subpart_type= partition_type::HASH;
    break;
  default:
    // Unknown sub partitioning type!
    DBUG_ASSERT(0); /* purecov: deadcode */
    return true;
  }

  dd::String_type part_expr= tab_obj->partition_expression();
  if (part_info->list_of_part_fields)
  {
    if (set_field_list(&share->mem_root,
                       part_expr,
                       &part_info->part_field_list))
    {
      return true;
    }
    part_info->part_func_string= NULL;
    part_info->part_func_len= 0;
  }
  else
  {
    part_info->part_func_string= strdup_root(&share->mem_root,
                                             part_expr.c_str());
    part_info->part_func_len= part_expr.length();
  }
  dd::String_type subpart_expr= tab_obj->subpartition_expression();
  part_info->subpart_func_len= subpart_expr.length();
  if (part_info->subpart_func_len)
  {
    if (part_info->list_of_subpart_fields)
    {
      if (set_field_list(&share->mem_root,
                         subpart_expr,
                         &part_info->subpart_field_list))
      {
        return true;
      }
      part_info->subpart_func_string= NULL;
      part_info->subpart_func_len= 0;
    }
    else
    {
      part_info->subpart_func_string= strdup_root(&share->mem_root,
                                                  subpart_expr.c_str());
    }
  }

  //
  // Iterate through all the partitions
  //

  partition_element *curr_part= NULL, *curr_part_elem;
  uint num_subparts= 0, part_id= 0, level= 0;
  bool is_subpart;
  List_iterator<partition_element> part_elem_it;

  /* Partitions are sorted first on level and then on number. */

  for (const dd::Partition *part_obj : tab_obj->partitions())
  {
    /* Must be in sorted order (sorted by level first and then on number). */
    DBUG_ASSERT(part_obj->level() >= level);
    DBUG_ASSERT(part_obj->number() >= part_id ||
                part_obj->level() > level);
    part_id= part_obj->number();
    level= part_obj->level();
    DBUG_ASSERT(level <= 1);
    is_subpart= (level != 0);
    curr_part_elem= new(&share->mem_root) partition_element;
    if (!curr_part_elem)
    {
      return true;
    }
    if (setup_partition_from_dd(thd,
                                &share->mem_root,
                                part_info,
                                curr_part_elem,
                                part_obj,
                                is_subpart))
    {
      return true;
    }

    if (!is_subpart)
    {
      DBUG_ASSERT(!curr_part);
      if (part_info->partitions.push_back(curr_part_elem, &share->mem_root))
        return true;
    }
    else
    {
      if (!curr_part)
      {
        /*
          First subpartition. Initialize partition iterator and calculate
          number of subpartitions per partition.
        */
        part_elem_it.init(part_info->partitions);
	num_subparts= (tab_obj->partitions().size() -
                       part_info->partitions.elements) /
                      part_info->partitions.elements;
      }
      /* Increment partition iterator for first subpartition in the partition. */
      if ((part_id % num_subparts) == 0)
        curr_part= part_elem_it++;
      if (curr_part->subpartitions.push_back(curr_part_elem, &share->mem_root))
        return true;
    }
  }
  part_info->num_parts= part_info->partitions.elements;
  if (curr_part)
  {
    part_info->num_subparts= curr_part->subpartitions.elements;
    DBUG_ASSERT(part_info->num_subparts == num_subparts);
  }
  else
    part_info->num_subparts= 0;

  switch(tab_obj->default_partitioning())
  {
  case dd::Table::DP_NO:
    part_info->use_default_partitions= false;
    part_info->use_default_num_partitions= false;
    break;
  case dd::Table::DP_YES:
    part_info->use_default_partitions= true;
    part_info->use_default_num_partitions= true;
    break;
  case dd::Table::DP_NUMBER:
    part_info->use_default_partitions= true;
    part_info->use_default_num_partitions= false;
    break;
  case dd::Table::DP_NONE:
  default:
    DBUG_ASSERT(0); /* purecov: deadcode */
  }
  switch(tab_obj->default_subpartitioning())
  {
  case dd::Table::DP_NO:
    part_info->use_default_subpartitions= false;
    part_info->use_default_num_subpartitions= false;
    break;
  case dd::Table::DP_YES:
    part_info->use_default_subpartitions= true;
    part_info->use_default_num_subpartitions= true;
    break;
  case dd::Table::DP_NUMBER:
    part_info->use_default_subpartitions= true;
    part_info->use_default_num_subpartitions= false;
    break;
  case dd::Table::DP_NONE:
    DBUG_ASSERT(!part_info->is_sub_partitioned());
    break;
  default:
    DBUG_ASSERT(0); /* purecov: deadcode */
  }

  char *buf;
  uint buf_len;

  buf= generate_partition_syntax(part_info,
                                 &buf_len,
                                 true,
                                 true,
                                 NULL,
                                 NULL,
                                 NULL);
  if (!buf)
    return true;

  share->partition_info_str= strmake_root(&share->mem_root, buf, buf_len);
  if (!share->partition_info_str)
    return true;

  share->partition_info_str_len= buf_len;
  share->m_part_info= part_info;
  return (false);
}


bool open_table_def(THD *thd, TABLE_SHARE *share, bool open_view,
                    const dd::Table *table_def)
{
  DBUG_ENTER("open_table_def");

  dd::cache::Dictionary_client::Auto_releaser releaser(thd->dd_client());

  // Assume base table, we find it is a view a bit later.
  dd::enum_table_type dd_table_type= dd::enum_table_type::BASE_TABLE;

  if (!table_def)
  {
    // Make sure the schema exists.
    bool exists= false;
    if (dd::schema_exists(thd, share->db.str, &exists))
      DBUG_RETURN(true);

    if (!exists)
    {
      my_error(ER_BAD_DB_ERROR, MYF(0), share->db.str);
      DBUG_RETURN(true);
    }

    if (dd::abstract_table_type(thd->dd_client(), share->db.str,
                                share->table_name.str,
                                &dd_table_type))
    {
      // Error is reported in dd_abstract_table_type().
      DBUG_RETURN(true);
    }

    if (dd_table_type == dd::enum_table_type::USER_VIEW ||
        dd_table_type == dd::enum_table_type::SYSTEM_VIEW)
    {
      if (!open_view)
      {
        // We found a view but were trying to open table only.
        my_error(ER_NO_SUCH_TABLE, MYF(0), share->db.str, share->table_name.str);
        DBUG_RETURN(true);
      }
      /*
        Create view reference object and hold it in TABLE_SHARE member view_object.
        Read it from DD
      */
      share->is_view= true;
      const dd::View *tmp_view= nullptr;
      if (thd->dd_client()->acquire(share->db.str,
                                    share->table_name.str,
                                    &tmp_view))
      {
        DBUG_ASSERT(thd->is_error() || thd->killed);
        DBUG_RETURN(true);
      }

      if (!tmp_view)
      {
        my_error(ER_NO_SUCH_TABLE, MYF(0), share->db.str, share->table_name.str);
        DBUG_RETURN(true);
      }
      share->view_object= tmp_view->clone();

      share->table_category= get_table_category(share->db, share->table_name);
      thd->status_var.opened_shares++;
      DBUG_RETURN(false);
    }
    else // BASE_TABLE
    {
      (void) thd->dd_client()->acquire(share->db.str,
                                       share->table_name.str,
                                       &table_def);
    }
  }

  if (!table_def)
  {
    DBUG_ASSERT(thd->is_error() || thd->killed);
    DBUG_RETURN(true);
  }

  MEM_ROOT *old_root= thd->mem_root;
  thd->mem_root= &share->mem_root; // Needed for make_field()++
  share->blob_fields= 0; // HACK

  // Fill the TABLE_SHARE with details.
  bool error=  (fill_share_from_dd(thd, share, table_def) ||
                fill_columns_from_dd(share, table_def) ||
                fill_indexes_from_dd(share, table_def) ||
                fill_partitioning_from_dd(thd, share, table_def));

  thd->mem_root= old_root;

  if (!error)
    error= prepare_share(thd, share);

  if (!error)
  {
    share->table_category= get_table_category(share->db, share->table_name);
    thd->status_var.opened_shares++;
    DBUG_RETURN(false);
  }
  DBUG_RETURN(true);
}

//////////////////////////////////////////////////////////////////////////

/**
  Check if Index_element represents prefix key part on the column.

  @note This function is in sync with how we evaluate HA_PART_KEY_SEG.
        As result it returns funny results for BLOB/GIS types (TODO/FIXME
        check this logic).

  TODO/FIXME: Consider making it proper method of Index_element.
*/

/* purecov: begin deadcode */
bool dd_index_element_is_prefix(const dd::Index_element *idx_el)
{
  uint interval_parts;
  const dd::Column& col= idx_el->column();
  enum_field_types field_type= dd_get_old_field_type(col.type());

  if (field_type == MYSQL_TYPE_ENUM || field_type == MYSQL_TYPE_SET)
    interval_parts= col.elements_count();
  else
    interval_parts= 0;

  return calc_key_length(field_type,
                         col.char_length(),
                         col.numeric_scale(),
                         col.is_unsigned(),
                         interval_parts) != idx_el->length();
}
/* purecov: end */


/**
  Check if Index represents candidate key.

  @note This function is in sync with how we evaluate TABLE_SHARE::primary_key.

  TODO/FIXME: Consider making it proper method of Index.
*/

/* purecov: begin deadcode */
bool dd_index_is_candidate_key(const dd::Index *idx_obj)
{
  if (idx_obj->type() != dd::Index::IT_PRIMARY &&
      idx_obj->type() != dd::Index::IT_UNIQUE)
    return false;

  for (const dd::Index_element *idx_elem_obj : idx_obj->elements())
  {
    // Skip hidden index elements
    if (idx_elem_obj->is_hidden())
      continue;

    if (idx_elem_obj->column().is_nullable())
      return false;

    /*
      Probably we should adjust is_prefix() to take these two scenarios
      into account. But this also means that we probably need avoid
      setting HA_PART_KEY_SEG in them.
    */

    if ((idx_elem_obj->column().type() == dd::enum_column_types::TINY_BLOB &&
         idx_elem_obj->length() == 255) ||
        (idx_elem_obj->column().type() == dd::enum_column_types::BLOB &&
         idx_elem_obj->length() == 65535) ||
        (idx_elem_obj->column().type() == dd::enum_column_types::MEDIUM_BLOB &&
         idx_elem_obj->length() == (1 << 24) - 1) ||
        (idx_elem_obj->column().type() == dd::enum_column_types::LONG_BLOB &&
         idx_elem_obj->length() == (1LL << 32) - 1))
      continue;

    if (idx_elem_obj->column().type() == dd::enum_column_types::GEOMETRY)
    {
      uint32 sub_type;
      idx_elem_obj->column().options().get_uint32("geom_type", &sub_type);
      if (sub_type ==  Field::GEOM_POINT &&
          idx_elem_obj->length() == MAX_LEN_GEOM_POINT_FIELD)
        continue;
    }


    if (dd_index_element_is_prefix(idx_elem_obj))
      return false;
  }
  return true;
}
/* purecov: end */
