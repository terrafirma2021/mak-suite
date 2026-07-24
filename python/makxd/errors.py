class MakxdError(Exception):
    pass

class MakxdConnectionError(MakxdError):
    pass

class MakxdCommandError(MakxdError):
    pass

class MakxdTimeoutError(MakxdError):
    pass

class MakxdResponseError(MakxdError):
    pass