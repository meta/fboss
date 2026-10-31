namespace cpp facebook.fboss.thrift
namespace cpp2 facebook.fboss.thrift
namespace go neteng.fboss
namespace py neteng.fboss
namespace py3 neteng.fboss
namespace py.asyncio neteng.asyncio.fboss

include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "fboss_"}
@hack.LegacyOmitPrefixInNameString
@thrift.AllowLegacyMissingUris
package;

@thrift.DeprecatedUnvalidatedAnnotations{items = {"cpp.virtual": "1"}}
exception FbossBaseError {
  @thrift.ExceptionMessage
  1: string message;
}

/*
 * How a service applies a new config, from least to most disruptive. A method
 * can make every change a less disruptive one can.
 */
enum ConfigApplyMethod {
  // Applied to the running service, e.g. the agent's reloadConfig().
  RELOAD = 0,
  // Restart the service, keeping the state it can, e.g. an agent warmboot.
  RESTART = 1,
  // Restart the service from scratch, e.g. an agent coldboot.
  DISRUPTIVE_RESTART = 2,
}

/*
 * One problem found while validating a candidate config.
 */
struct ConfigValidationError {
  1: string message;
  // Set when the config is valid but this change cannot be made with the
  // apply method that was validated for: the least disruptive method that
  // can make it.
  2: optional ConfigApplyMethod requiredApplyMethod;
}

/*
 * Verdict of a service's validateConfig() on a candidate config. Shared by
 * the services the CLI configures, so it can handle them all the same way.
 */
struct ConfigValidationResult {
  // Empty if the config would be accepted.
  1: list<ConfigValidationError> errors;
}
