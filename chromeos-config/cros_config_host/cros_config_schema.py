#!/usr/bin/env python2
# -*- coding: utf-8 -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Transforms and validates cros config from source YAML to target JSON"""

from __future__ import print_function

import argparse
import collections
import copy
from jinja2 import Template
import json
import math
import os
import re
import sys
import yaml

import  libcros_schema

this_dir = os.path.dirname(__file__)

CHROMEOS = 'chromeos'
CONFIGS = 'configs'
DEVICES = 'devices'
PRODUCTS = 'products'
SKUS = 'skus'
CONFIG = 'config'
BRAND_ELEMENTS = ['brand-code', 'firmware-signing', 'wallpaper',
                  'regulatory-label']
TEMPLATE_PATTERN = re.compile('{{([^}]*)}}')

EC_OUTPUT_NAME = 'ec_config'
MOSYS_OUTPUT_NAME = 'config.c'
TEMPLATE_DIR = 'templates'
TEMPLATE_SUFFIX = '.jinja2'


def MergeDictionaries(primary, overlay):
  """Merges the overlay dictionary onto the primary dictionary.

  If an element doesn't exist, it's added.
  If the element is a list, they are appended to each other.
  Otherwise, the overlay value takes precedent.

  Args:
    primary: Primary dictionary
    overlay: Overlay dictionary
  """
  for overlay_key in overlay.keys():
    overlay_value = overlay[overlay_key]
    if not overlay_key in primary:
      primary[overlay_key] = overlay_value
    elif isinstance(overlay_value, collections.Mapping):
      MergeDictionaries(primary[overlay_key], overlay_value)
    elif isinstance(overlay_value, list):
      primary[overlay_key].extend(overlay_value)
    else:
      primary[overlay_key] = overlay_value


def ParseArgs(argv):
  """Parse the available arguments.

  Invalid arguments or -h cause this function to print a message and exit.

  Args:
    argv: List of string arguments (excluding program name / argv[0])

  Returns:
    argparse.Namespace object containing the attributes.
  """
  parser = argparse.ArgumentParser(
      description='Validates a YAML cros-config and transforms it to JSON')
  parser.add_argument(
      '-s',
      '--schema',
      type=str,
      help='Path to the schema file used to validate the config')
  parser.add_argument(
      '-c',
      '--config',
      type=str,
      help='Path to the YAML config file that will be validated/transformed')
  parser.add_argument(
      '-m',
      '--configs',
      nargs='+',
      type=str,
      help='Path to the YAML config file(s) that will be validated/transformed')
  parser.add_argument(
      '-o',
      '--output',
      type=str,
      help='Output file that will be generated by the transform (system file)')
  parser.add_argument(
      '-g',
      '--generated_c_output_directory',
      type=str,
      help='Directory where generated C config code should be placed')
  parser.add_argument(
      '-f',
      '--filter',
      type=bool,
      default=False,
      help='Filter build specific elements from the output JSON')
  return parser.parse_args(argv)


def _SetTemplateVars(template_input, template_vars):
  """Builds a map of template variables by walking the input recursively.

  Args:
    template_input: A mapping object to be walked.
    template_vars: A mapping object built up while walking the template_input.
  """
  to_add = {}
  for key, val in template_input.iteritems():
    if isinstance(val, collections.Mapping):
      _SetTemplateVars(val, template_vars)
    elif not isinstance(val, list):
      to_add[key] = val

  # Do this last so all variables from the parent scope win.
  template_vars.update(to_add)


def _GetVarTemplateValue(val, template_input, template_vars):
  """Applies the templating scheme to a single value.

  Args:
    val: The single val to evaluate.
    template_input: Input that will be updated based on the templating schema.
    template_vars: A mapping of all the variables values available.

  Returns:
    The variable value with templating applied.
  """
  for template_var in TEMPLATE_PATTERN.findall(val):
    replace_string = '{{%s}}' % template_var
    if template_var not in template_vars:
      formatted_vars = json.dumps(template_vars, sort_keys=True, indent=2)
      formatted_input = json.dumps(template_input, sort_keys=True, indent=2)
      error_vals = (template_var, val, formatted_input, formatted_vars)
      raise ValidationError("Referenced template variable '%s' doesn't "
                            "exist string '%s'.\nInput:\n %s\nVariables:\n%s" %
                            error_vals)
    var_value = template_vars[template_var]

    # This is an ugly side effect of templating with primitive values.
    # The template is a string, but the target value needs to be int.
    # This is sort of a hack for now, but if the problem gets worse, we
    # can come up with a more scaleable solution.
    #
    # Guessing this problem won't continue though beyond the use of 'sku-id'
    # since that tends to be the only strongly typed value due to its use
    # for identity detection.
    is_int = isinstance(var_value, int)
    if is_int:
      var_value = str(var_value)

    # If the caller only had one value and it was a template variable that
    # was an int, assume the caller wanted the string to be an int.
    if is_int and val == replace_string:
      val = template_vars[template_var]
    else:
      val = val.replace(replace_string, var_value)
  return val


def _ApplyTemplateVars(template_input, template_vars):
  """Evals the input and applies the templating schema using the provided vars.

  Args:
    template_input: Input that will be updated based on the templating schema.
    template_vars: A mapping of all the variables values available.
  """
  maps = []
  lists = []
  for key in template_input.keys():
    val = template_input[key]
    if isinstance(val, collections.Mapping):
      maps.append(val)
    elif isinstance(val, list):
      index = 0
      for list_val in val:
        if isinstance(list_val, collections.Mapping):
          lists.append(list_val)
        elif isinstance(list_val, basestring):
          val[index] = _GetVarTemplateValue(list_val, template_input,
                                            template_vars)
        index += 1
    elif isinstance(val, basestring):
      template_input[key] = _GetVarTemplateValue(val, template_input,
                                                 template_vars)

  # Do this last so all variables from the parent are in scope first.
  for value in maps:
    _ApplyTemplateVars(value, template_vars)

  # Object lists need their variables put in scope on a per list item basis
  for value in lists:
    list_item_vars = copy.deepcopy(template_vars)
    _SetTemplateVars(value, list_item_vars)
    while _HasTemplateVariables(list_item_vars):
      _ApplyTemplateVars(list_item_vars, list_item_vars)
    _ApplyTemplateVars(value, list_item_vars)


def _DeleteTemplateOnlyVars(template_input):
  """Deletes all variables starting with $

  Args:
    template_input: Input that will be updated based on the templating schema.
  """
  to_delete = []
  for key in template_input.keys():
    val = template_input[key]
    if isinstance(val, collections.Mapping):
      _DeleteTemplateOnlyVars(val)
    elif isinstance(val, list):
      for v in val:
        if isinstance(v, collections.Mapping):
          _DeleteTemplateOnlyVars(v)
    elif key.startswith('$'):
      to_delete.append(key)

  for key in to_delete:
    del template_input[key]


def _HasTemplateVariables(template_vars):
  """Checks if there are any unevaluated template variables.

  Args:
    template_vars: A mapping of all the variables values available.

  Returns:
    True if they are still unevaluated template variables.
  """
  for val in template_vars.values():
    if isinstance(val, basestring) and len(TEMPLATE_PATTERN.findall(val)) > 0:
      return True


def TransformConfig(config, model_filter_regex=None):
  """Transforms the source config (YAML) to the target system format (JSON)

  Applies consistent transforms to covert a source YAML configuration into
  JSON output that will be used on the system by cros_config.

  Args:
    config: Config that will be transformed.
    model_filter_regex: Only returns configs that match the filter

  Returns:
    Resulting JSON output from the transform.
  """
  config_yaml = yaml.load(config)
  json_from_yaml = json.dumps(config_yaml, sort_keys=True, indent=2)
  json_config = json.loads(json_from_yaml)
  configs = []
  if DEVICES in json_config[CHROMEOS]:
    for device in json_config[CHROMEOS][DEVICES]:
      template_vars = {}
      for product in device.get(PRODUCTS, [{}]):
        for sku in device[SKUS]:
          # Template variables scope is config, then device, then product
          # This allows shared configs to define defaults using anchors, which
          # can then be easily overridden by the product/device scope.
          _SetTemplateVars(sku, template_vars)
          _SetTemplateVars(device, template_vars)
          _SetTemplateVars(product, template_vars)
          while _HasTemplateVariables(template_vars):
            _ApplyTemplateVars(template_vars, template_vars)
          sku_clone = copy.deepcopy(sku)
          _ApplyTemplateVars(sku_clone, template_vars)
          config = sku_clone[CONFIG]
          _DeleteTemplateOnlyVars(config)
          configs.append(config)
  else:
    configs = json_config[CHROMEOS][CONFIGS]

  if model_filter_regex:
    matcher = re.compile(model_filter_regex)
    configs = [
        config for config in configs if matcher.match(config['name'])
    ]

  # Drop everything except for configs since they were just used as shared
  # config in the source yaml.
  json_config = {
      CHROMEOS: {
          CONFIGS: configs,
      },
  }

  return libcros_schema.FormatJson(json_config)

def _IsLegacyMigration(platform_name):
  """Determines if the platform was migrated from FDT impl.

  Args:
    platform_name: Platform name to be checked
  """
  return platform_name in ['Coral', 'Fizz']

def GenerateMosysCBindings(config):
  """Generates Mosys C struct bindings

  Generates C struct bindings that can be used by mosys.

  Args:
    config: Config (transformed) that is the transform basis.
  """
  struct_format = '''
    {.platform_name = "%s",
     .firmware_name_match = "%s",
     .sku_id = %s,
     .customization_id = "%s",
     .whitelabel_tag = "%s",
     .info = {.brand = "%s",
              .model = "%s",
              .customization = "%s",
              .signature_id = "%s"}}'''
  structs = []
  json_config = json.loads(config)
  for config in json_config[CHROMEOS][CONFIGS]:
    identity = config['identity']
    name = config['name']
    whitelabel_tag = identity.get('whitelabel-tag', '')
    customization_id = identity.get('customization-id', '')
    customization = customization_id or whitelabel_tag or name
    signature_id = config.get('firmware-signing', {}).get('signature-id', '')
    signature_id = signature_id or name
    brand_code = config.get('brand-code', '')
    platform_name = identity.get('platform-name', '')
    sku_id = identity.get('sku-id', -1)
    if _IsLegacyMigration(platform_name):
      # The mosys device-tree impl hard-coded this logic.
      # Since we've already launched without this on new platforms,
      # we have to keep to special backwards compatibility.
      if whitelabel_tag:
        customization = ('%s-%s' % (name, customization))
      customization = customization.upper()

    # At most one of <device_tree_compatible_match> and <smbios-name-match>
    # should be set (depends on whether this is for ARM or x86). This is used as
    #  <firmware_name_match> for mosys.
    firmware_name_match = identity.get('device-tree-compatible-match',
                                       identity.get('smbios-name-match', ''))
    structs.append(
        struct_format % (platform_name,
                         firmware_name_match,
                         sku_id,
                         customization_id,
                         whitelabel_tag,
                         brand_code,
                         name,
                         customization,
                         signature_id))

  file_format = '''\
#include "lib/cros_config_struct.h"

static struct config_map all_configs[] = {%s
};

const struct config_map *cros_config_get_config_map(int *num_entries) {
  *num_entries = %s;
  return &all_configs[0];
}'''

  return file_format % (',\n'.join(structs), len(structs))


def GenerateEcCBindings(config, schema_yaml):
  """Generates EC C struct bindings

  Generates .h and .c file containing C struct bindings that can be used by ec.

  Args:
    config: Config (transformed) that is the transform basis.
    schema_yaml: Cros_config_schema in yaml format.
  """

  json_config = json.loads(config)
  device_properties = collections.defaultdict(dict)
  # Store the number of bits required for a hwprop's value.
  hwprop_values_count = collections.defaultdict(int)
  # Store a list of the elements for every enum. This
  # will be used in the ec_config.h auto-generation code.
  enum_to_elements_map = collections.defaultdict(list)
  hwprop_set = set()
  for config in json_config[CHROMEOS][CONFIGS]:
    firmware = config['firmware']

    if 'build-targets' not in firmware:
      # Do not consider it an error if a config explicitly specifies no
      # firmware.
      if 'no-firmware' not in firmware:
        print("WARNING: config missing 'firmware.build-targets', skipping",
              file=sys.stderr)
    elif 'ec' not in firmware['build-targets']:
      print("WARNING: config missing 'firmware.build-targets.ec', skipping",
            file=sys.stderr)
    elif 'identity' not in config:
      print("WARNING: config missing 'identity', skipping",
            file=sys.stderr)
    elif 'sku-id' not in config['identity']:
      print("WARNING: config missing 'identity.sku-id', skipping",
            file=sys.stderr)
    else:
      sku = config['identity']['sku-id']
      ec_build_target = firmware['build-targets']['ec'].upper()

      # Default hwprop value will be false.
      hwprop_values = collections.defaultdict(bool)

      hwprops = config.get('hardware-properties', None)
      if hwprops:
        # |hwprop| is a user specified property of the hardware, for example
        # 'is-lid-convertible', which means that the device can rotate 360.
        for hwprop, value in hwprops.items():
          # Convert the name of the hwprop to a valid C identifier.
          clean_hwprop = hwprop.replace('-', '_')
          hwprop_set.add(clean_hwprop)
          if isinstance(value, bool):
            hwprop_values_count[clean_hwprop] = 1
            hwprop_values[clean_hwprop] = value
          elif isinstance(value, unicode):
            # Calculate the number of bits by taking the log_2 of the number
            # of possible enumerations. Use math.ceil to round up.
            # For example, if an enum has 7 possible values (elements, we will
            # need 3 bits to represent all the values.
            # log_2(7) ~= 2.807 -> round up to 3.
            element_to_int_map = _GetElementToIntMap(schema_yaml, hwprop)
            if value not in element_to_int_map:
              raise ValidationError('Not a valid enum value: %s' % value)
            enum_to_elements_map[clean_hwprop] = element_to_int_map
            element_count = len(element_to_int_map)
            hwprop_values_count[clean_hwprop] = int(
                math.ceil(math.log(element_count)))
            hwprop_values[clean_hwprop] = element_to_int_map[value]

      # Duplicate skus take the last value in the config file.
      device_properties[ec_build_target][sku] = hwprop_values

  hwprops = list(hwprop_set)
  hwprops.sort()
  for ec_build_target in device_properties.iterkeys():
    # Order struct definitions by sku.
    device_properties[ec_build_target] = \
        sorted(device_properties[ec_build_target].items())

  h_template_path = os.path.join(
      this_dir, TEMPLATE_DIR, (EC_OUTPUT_NAME + '.h' + TEMPLATE_SUFFIX))
  h_template = Template(open(h_template_path).read())

  c_template_path = os.path.join(
      this_dir, TEMPLATE_DIR, (EC_OUTPUT_NAME + '.c' + TEMPLATE_SUFFIX))
  c_template = Template(open(c_template_path).read())

  h_output = h_template.render(
      hwprops=hwprops,
      hwprop_values_count=hwprop_values_count,
      enum_to_elements_map=enum_to_elements_map)
  c_output = c_template.render(
      device_properties=device_properties, hwprops=hwprops)
  return (h_output, c_output)


def _GetElementToIntMap(schema_yaml, hwprop):
  """Returns a mapping of an enum's elements to a distinct integer.

  Used in the c_template to assign an integer to
  the stylus category type.

  Args:
    schema_yaml: Cros_config_schema in yaml format.
    hwprop: String representing the hardware property
    of the enum (ex. stylus-category)
  """
  schema_json_from_yaml = libcros_schema.FormatJson(schema_yaml)
  schema_json = json.loads(schema_json_from_yaml)
  if hwprop not in schema_json['typeDefs']:
    raise ValidationError('Hardware property not found: %s' % str(hwprop))
  if "enum" not in schema_json["typeDefs"][hwprop]:
    raise ValidationError('Hardware property is not an enum: %s' % str(hwprop))
  return dict((element, i) for (i, element) in enumerate(
      schema_json["typeDefs"][hwprop]["enum"]))

def FilterBuildElements(config, build_only_elements):
  """Removes build only elements from the schema.

  Removes build only elements from the schema in preparation for the platform.

  Args:
    config: Config (transformed) that will be filtered
    build_only_elements: List of strings of paths of fields to be filtered
  """
  json_config = json.loads(config)
  for config in json_config[CHROMEOS][CONFIGS]:
    _FilterBuildElements(config, '', build_only_elements)

  return libcros_schema.FormatJson(json_config)


def _FilterBuildElements(config, path, build_only_elements):
  """Recursively checks and removes build only elements.

  Args:
    config: Dict that will be checked.
    path: Path of elements to filter.
    build_only_elements: List of strings of paths of fields to be filtered
  """
  to_delete = []
  for key in config:
    full_path = '%s/%s' % (path, key)
    if full_path in build_only_elements:
      to_delete.append(key)
    elif isinstance(config[key], dict):
      _FilterBuildElements(config[key], full_path, build_only_elements)
  for key in to_delete:
    config.pop(key)


def GetValidSchemaProperties(
    schema=os.path.join(this_dir, 'cros_config_schema.yaml')):
  """Returns all valid properties from the given schema

  Iterates over the config payload for devices and returns the list of
  valid properties that could potentially be returned from
  cros_config_host or cros_config

  Args:
    schema: Source schema that contains the properties.
  """
  with open(schema, 'r') as schema_stream:
    schema_yaml = yaml.load(schema_stream.read())
  root_path = 'properties/chromeos/properties/configs/items/properties'
  schema_node = schema_yaml
  for element in root_path.split('/'):
    schema_node = schema_node[element]

  result = {}
  _GetValidSchemaProperties(schema_node, [], result)
  return result


def _GetValidSchemaProperties(schema_node, path, result):
  """Recursively finds the valid properties for a given node

  Args:
    schema_node: Single node from the schema
    path: Running path that a given node maps to
    result: Running collection of results
  """
  full_path = '/%s' % '/'.join(path)
  valid_schema_property_types = {'array', 'boolean', 'integer', 'string'}
  for key in schema_node:
    new_path = path + [key]
    node_type = schema_node[key]['type']

    if node_type == 'object':
      if 'properties' in schema_node[key]:
        _GetValidSchemaProperties(
            schema_node[key]['properties'], new_path, result)
    elif node_type in valid_schema_property_types:
      all_props = result.get(full_path, [])
      all_props.append(key)
      result[full_path] = all_props


class ValidationError(Exception):
  """Exception raised for a validation error"""
  pass


def _ValidateUniqueIdentities(json_config):
  """Verifies the identity tuple is globally unique within the config.

  Args:
    json_config: JSON config dictionary
  """
  identities = set()
  duplicate_identities = set()
  for config in json_config['chromeos']['configs']:
    if 'identity' not in config and 'name' not in config:
      raise ValidationError(
          'Missing identity for config: %s' % str(config))
    identity_str = "%s-%s" % (
        config.get('name', ''), str(config.get('identity', {})))
    if identity_str in identities:
      duplicate_identities.add(identity_str)
    else:
      identities.add(identity_str)

  if duplicate_identities:
    raise ValidationError(
        'Identities are not unique: %s' % duplicate_identities)


def _ValidateWhitelabelBrandChangesOnly(json_config):
  """Verifies that whitelabel changes are contained to branding information.

  Args:
    json_config: JSON config dictionary
  """
  whitelabels = {}
  for config in json_config['chromeos']['configs']:
    whitelabel_tag = config.get('identity', {}).get('whitelabel-tag', None)
    if whitelabel_tag:
      name = '%s - %s' % (config['name'], config['identity'].get('sku-id', 0))
      config_list = whitelabels.get(name, [])

      wl_minus_brand = copy.deepcopy(config)
      wl_minus_brand['identity']['whitelabel-tag'] = ''

      for brand_element in BRAND_ELEMENTS:
        wl_minus_brand[brand_element] = ''

      config_list.append(wl_minus_brand)
      whitelabels[name] = config_list

    # whitelabels now contains a map by device name with all whitelabel
    # configs that have had their branding data stripped.
    for device_name, configs in whitelabels.iteritems():
      base_config = configs[0]
      compare_index = 1
      while compare_index < len(configs):
        compare_config = configs[compare_index]
        compare_index = compare_index + 1
        base_str = str(base_config)
        compare_str = str(compare_config)
        if base_str != compare_str:
          raise ValidationError(
              'Whitelabel configs can only change branding attributes (%s).\n'
              'However, the device %s differs by other attributes.\n'
              'Example 1: %s\n'
              'Example 2: %s' % (device_name,
                                 ', '.join(BRAND_ELEMENTS),
                                 base_str,
                                 compare_str))


def _ValidateHardwarePropertiesAreValidType(json_config):
  """Checks that all fields under hardware-properties are boolean

     Ensures that no key is added to hardware-properties that has a non-boolean
     value, because non-boolean values are unsupported by the
     hardware-properties codegen.

  Args:
    json_config: JSON config dictionary
  """
  for config in json_config['chromeos']['configs']:
    hardware_properties = config.get('hardware-properties', None)
    if hardware_properties:
      for key, value in hardware_properties.iteritems():
        valid_type = isinstance(value, bool) or isinstance(value, unicode)
        if not valid_type:
          raise ValidationError(
              ('All configs under hardware-properties must be '
               'boolean or an enum\n'
               'However, key \'{}\' has value \'{}\'.').format(key, value))


def ValidateConfig(config):
  """Validates a transformed cros config for general business rules.

  Performs name uniqueness checks and any other validation that can't be
  easily performed using the schema.

  Args:
    config: Config (transformed) that will be verified.
  """
  json_config = json.loads(config)
  _ValidateUniqueIdentities(json_config)
  _ValidateWhitelabelBrandChangesOnly(json_config)
  _ValidateHardwarePropertiesAreValidType(json_config)


def MergeConfigs(configs):
  """Evaluates and merges all config files into a single configuration.

  Args:
    configs: List of source config files that will be transformed/merged.

  Returns:
    Final merged JSON result.
  """
  json_files = []
  for yaml_file in configs:
    yaml_with_imports = libcros_schema.ApplyImports(yaml_file)
    json_transformed_file = TransformConfig(yaml_with_imports)
    json_files.append(json.loads(json_transformed_file))

  result_json = json_files[0]
  for overlay_json in json_files[1:]:
    for to_merge_config in overlay_json['chromeos']['configs']:
      to_merge_identity = to_merge_config.get('identity', {})
      to_merge_name = to_merge_config.get('name', '')
      matched = False
      # Find all existing configs where there is a full/partial identity
      # match or name match and merge that config into the source.
      # If there are no matches, then append the config.
      for source_config in result_json['chromeos']['configs']:
        identity_match = False
        if to_merge_identity:
          source_identity = source_config['identity']
          identity_match = True
          for identity_key, identity_value in to_merge_identity.iteritems():
            if (identity_key not in source_identity or
                source_identity[identity_key] != identity_value):
              identity_match = False
              break
        elif to_merge_name:
          identity_match = to_merge_name == source_config.get('name', '')

        if identity_match:
          MergeDictionaries(source_config, to_merge_config)
          matched = True

      if not matched:
        result_json['chromeos']['configs'].append(to_merge_config)

  return libcros_schema.FormatJson(result_json)


def Main(schema,
         config,
         output,
         filter_build_details=False,
         gen_c_output_dir=None,
         configs=None):
  """Transforms and validates a cros config file for use on the system

  Applies consistent transforms to covert a source YAML configuration into
  a JSON file that will be used on the system by cros_config.

  Verifies that the file complies with the schema verification rules and
  performs additional verification checks for config consistency.

  Args:
    schema: Schema file used to verify the config.
    config: Source config file that will be transformed/verified.
    output: Output file that will be generated by the transform.
    filter_build_details: Whether build only details should be filtered or not.
    gen_c_output_dir: Output directory for generated C config files.
    configs: List of source config files that will be transformed/verified.
  """
  if not schema:
    schema = os.path.join(this_dir, 'cros_config_schema.yaml')

  # TODO(shapiroc): Remove this once we no longer need backwards compatibility
  # for single config parameters.
  if config:
    configs = [config]

  full_json_transform = MergeConfigs(configs)
  json_transform = full_json_transform

  with open(schema, 'r') as schema_stream:
    schema_contents = schema_stream.read()
    libcros_schema.ValidateConfigSchema(schema_contents, json_transform)
    ValidateConfig(json_transform)
    schema_attrs = libcros_schema.GetSchemaPropertyAttrs(
        yaml.load(schema_contents))

    if filter_build_details:
      build_only_elements = []
      for path in schema_attrs:
        if schema_attrs[path].build_only_element:
          build_only_elements.append(path)
      json_transform = FilterBuildElements(json_transform, build_only_elements)
  if output:
    with open(output, 'w') as output_stream:
      # Using print function adds proper trailing newline.
      print(json_transform, file=output_stream)
  else:
    print(json_transform)
  if gen_c_output_dir:
    with open(os.path.join(gen_c_output_dir, MOSYS_OUTPUT_NAME), 'w') \
    as output_stream:
      # Using print function adds proper trailing newline.
      print(GenerateMosysCBindings(full_json_transform), file=output_stream)
    h_output, c_output = GenerateEcCBindings(
        full_json_transform, schema_yaml=yaml.load(schema_contents))
    with open(os.path.join(gen_c_output_dir, EC_OUTPUT_NAME + ".h"), 'w') \
    as output_stream:
      print(h_output, file=output_stream)
    with open(os.path.join(gen_c_output_dir, EC_OUTPUT_NAME + ".c"), 'w') \
    as output_stream:
      print(c_output, file=output_stream)

# The distutils generated command line wrappers will not pass us argv.
def main(argv=None):
  """Main program which parses args and runs

  Args:
    argv: List of command line arguments, if None uses sys.argv.
  """
  if argv is None:
    argv = sys.argv[1:]
  opts = ParseArgs(argv)
  Main(opts.schema, opts.config, opts.output, opts.filter,
       opts.generated_c_output_directory, opts.configs)

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
