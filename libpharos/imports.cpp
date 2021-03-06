// Copyright 2015 Carnegie Mellon University.  See LICENSE file for terms.

#include <boost/foreach.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>

#include <rose.h>

#include "imports.hpp"
#include "util.hpp"

ImportDescriptor::ImportDescriptor(std::string d, SgAsmPEImportItem *i) {
  item = i;
  address = item->get_iat_entry_va();
  std::string iname = item->get_name()->get_string();
  if (iname != "") {
    name = iname;
  }
  else {
    name = "INVALID";
  }
  dll = d;
  ordinal = item->get_ordinal();
  if (ordinal != 0) {
    GTRACE << "Import by ordinal at: " << addr_str(address)
           << " to " << get_normalized_name() << LEND;
  }

  // Create a new variable value to represent the memory initialized by the loader.
  loader_variable = SymbolicValue::loader_defined(get_arch_bits());
}

std::string ImportDescriptor::get_dll_root() const {
  std::string root;
  BOOST_FOREACH(char c, get_dll_name()) if (c == '.') break; else root += c;
  return root;
}

void ImportDescriptor::read_config(const boost::property_tree::ptree& tree) {
  // Address.  I'm somewhat uncertain about when we would need to override this.
  boost::optional<rose_addr_t> addr = tree.get_optional<rose_addr_t>("address");
  if (addr) {
    if (address != 0 && address != *addr) {
      GWARN << "Contradictory address in call "
            << addr_str(address) << "!=" << std::hex << addr << std::dec << LEND;
    }
    else {
      address = *addr;
    }
  }

  // The import name and DLL.  I'm uncertain about when we would need to override this.
  boost::optional<std::string> namestr = tree.get_optional<std::string>("name");
  if (namestr) name = *namestr;
  boost::optional<std::string> dllstr = tree.get_optional<std::string>("dll");
  if (dllstr) dll = *dllstr;
  boost::optional<size_t> ordnum = tree.get_optional<size_t>("ordinal");
  if (ordnum) {
    ordinal = *ordnum;
    GTRACE << "Read ordinal from config " << *ordnum << " and " << ordinal
           << " for dll=" << dll << " name=" << name << LEND;
  }

  // Read the function descriptor from the ptree branch "function".
  boost::optional<const boost::property_tree::ptree&> ftree = tree.get_child_optional("function");
  if (ftree) function_descriptor.read_config(ftree.get());

  // Callers needs to be copied from elsewhere.
  read_config_addr_set("callers", tree, callers);
}

void ImportDescriptor::set_names_hack(std::string compound_name) {
  // Cory thinks this is really messed up now. :-( See descriptors.cpp::resolve_imports() for
  // where we need to call this hacked up method.

  // We used to be reading the dll name and import name out of the "dll" and "name" fields of
  // the JSON field, but we apparently decided that was duplicative, and didn't generate those
  // fields for all of JSON configs imported from IDA.  Apparently we've been getting by using
  // only the name field keying the JSON entry for the import, so the "dll" and "name" fields
  // were never filled in except by the program being analyzed, which we merged into the
  // descriptor AFTER finding the appropriate entry in the map.  Now we have to split the name
  // field on the colon, which is something I was trying to avoid doing.  When we redesign this
  // JSON format, we should come up with a better plan here.  Perhaps we read the config, join
  // the two parts together, and then key the map with the joined string?

  // Fill in the dll and name fields now.  If they're also in the JSON config file,
  // that will overwrite these values, which is probably a good thing.
  size_t colon_pos = compound_name.find(':');
  if (colon_pos != std::string::npos) {
    dll = compound_name.substr(0, colon_pos);
    name = compound_name.substr(colon_pos + 1);
  }
}

// Merge the important fields from the export descriptor loaded from a DLL config file.
void ImportDescriptor::merge_export_descriptor(ImportDescriptor* did) {
  // This debugging message duplicates the work in function descriptor version of this method
  // that we call next but Cory wanted to be able to report the import name, which is only
  // conveniently accessible from this method on the import descriptor. :-(
  const FunctionDescriptor *dfd = did->get_function_descriptor();
  StackDelta stack_delta = dfd->get_stack_delta();
  GDEBUG << "Setting stack delta to " << stack_delta << " for import " << get_long_name()
         << " at address " << address_string() << LEND;

  // There are other fields to be merged here now as well.  We've potentially got a confusing
  // mixture of name, dll, and ordinal is various combinations and mixed cases.  In general,
  // Cory thinks the behavior we want here is to import anything that's _missing_ from the file
  // being analyzed (like perhaps the import name), but not to overwrite values from the file
  // being analyzed.  It's unclear happens and what should be done when the ordinal and the
  // name are contradictory for example.

  // Probably the most important case, where the input file tried to be sneaky and import by
  // ordinal, but we've figured out what the corrsponding name is?
  if (name == "INVALID") name = did->get_name();
  // Not possible because we must have a DLL name in the input file and have reached here?
  if (dll == "INVALID") dll = did->get_dll_name();
  // Pretty common, but not very useful?  (We now know the ordinal that was not specified?)
  if (ordinal == 0) ordinal = did->get_ordinal();

  // Here's the real call that does most of the work.
  function_descriptor.merge_export_descriptor(dfd);
}

std::string ImportDescriptor::get_normalized_name() const {
  // Return the name if we have one.
  if (name != "INVALID") {
    return to_lower(dll) + ":" + to_lower(name);
  }
  // Then try by ordinal.
  if (ordinal != 0) {
    return to_lower(dll) + ":" + str(boost::format("%d") % ordinal);;
  }
  // Fall back to the name, even if it's invalid.
  return to_lower(dll) + ":" + to_lower(name);
}

// Report the "best available" name.  This is the case sensitive version of the name if it
// exists, and the ordinal pseudo-name if it doesn't.
std::string ImportDescriptor::get_best_name() const {
  // Return the name if we have one.
  if (name != "INVALID") {
    return dll + ":" + name;
  }
  // Then try by ordinal.
  if (ordinal != 0) {
    return dll + ":" + str(boost::format("%d") % ordinal);
  }
  // Fall back to the name, even if it's invalid.
  return dll + ":" + name;
}

void ImportDescriptor::write_config(boost::property_tree::ptree* tree) {
  if (address != 0) tree->put("address", address_string());
  // These are sort of duplicative.
  tree->put("name", name);
  tree->put("dll", dll);
  // Ordinal output is untested.
  tree->put("ordinal", ordinal);

  // Record the callers.
  write_config_addr_set("callers", tree, callers);

  // Our function.
  boost::property_tree::ptree ftree;
  function_descriptor.write_config(&ftree);
  tree->put_child("function", ftree);
}

void ImportDescriptor::print(std::ostream &o) const {
  o << "Import: addr=" << std::hex << get_address() << std::dec
    << " name='" << get_long_name() << "'"
    << function_descriptor.debug_deltas()
    << " callers=[" << std::hex;
  for (CallTargetSet::iterator cit = callers.begin(); cit != callers.end(); cit++) {
    o << str(boost::format(" 0x%08X") % *cit);
  }
  o << " ]" << std::dec;
}

void ImportDescriptor::validate(std::ostream &o) {
  if (callers.size() == 0)
    o << "No callers for " << *this << LEND;
}

ImportDescriptor* ImportDescriptorMap::find_name(std::string dll, std::string name) {
  std::string normed = to_lower(dll) + ":" + to_lower(name);

  BOOST_FOREACH(ImportDescriptorMap::value_type& pair, *this) {
    ImportDescriptor& id = pair.second;
    if (id.get_normalized_name() == normed) {
      return &id;
    }
  }
  return NULL;
}

ImportDescriptorSet ImportDescriptorMap::find_name(std::string name) {
  ImportDescriptorSet ids;
  std::string lname = to_lower(name);

  BOOST_FOREACH(ImportDescriptorMap::value_type& pair, *this) {
    ImportDescriptor& id = pair.second;
    if (to_lower(id.get_name()) == lname) {
      ids.insert(&id);
    }
  }

  return ids;
}

/* Local Variables:   */
/* mode: c++          */
/* fill-column:    95 */
/* comment-column: 0  */
/* End:               */
