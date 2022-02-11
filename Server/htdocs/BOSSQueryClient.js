//JS module retrieving data from BOSS
//it manages UrlQueries and support graphic queries

export class BOSSConnectionConfig {
    constructor(myBOSSServerUrl, myBOSSRestEndPoint) {
        this.BOSSServerUrl = myBOSSServerUrl;
        this.BOSSRestEndPoint = myBOSSRestEndPoint;
        this.BOSSRestUrl = this.BOSSServerUrl + "/" + this.BOSSRestEndPoint + "/";
    }
}

export function drawBOSSChart(drawFunction, updateFunction, queryUrl, queryInterval, config, drawParams = "") {
    BOSSQuery(queryUrl, config).then((data) => {
        drawFunction(data, drawParams);
    }).then(() => {
        return setTimeout(function update() {
            BOSSQuery(queryUrl, config).then((data) => {
                updateFunction(data, drawParams);
            }).then(() => {
                setTimeout(update, queryInterval);
            })
        }, queryInterval);
    });
}

export function drawBOSSChartFromElementId(drawFunction, updateFunction, errorFunction, elementId, getQuery, queryInterval, config, drawParams = "") {
    let queryUrl = getQuery(elementId);
    let oldQuery = queryUrl;
    BOSSQuery(queryUrl, config).then((data) => {
        drawFunction(data, drawParams);
    }).then(() => {
        return setTimeout(function update() {
            let queryUrl = getQuery(elementId);
            if (oldQuery != queryUrl) {
                drawBOSSChartFromElementId(drawFunction, updateFunction, errorFunction, elementId, getQuery, queryInterval, config);
                return;
            }
            BOSSQuery(queryUrl, config).then((data) => {
                updateFunction(data, drawParams);
            }).then(() => {
                setTimeout(update, queryInterval);
            }).catch((data) => {
                errorFunction(data, elementId);
            });
        }, queryInterval);
    }).catch((data) => {
        errorFunction(data, elementId);
    });
}

export async function BOSSQuery(urlQuery, config) {
    if (urlQuery == "") {
        return new Promise((resolve, reject) => {
            var data = [{
                'query': urlQuery,
                'executed': 'no'
            }]
            reject(JSON.stringify(data));
        });
    }
    var url = config.BOSSRestUrl + urlQuery;
    return new Promise((resolve, reject) => {
        fetch(url)
            .then((res) => {
                res.json().then((data) => {
                    if (res.ok) {
                        data = JSON.stringify(data);
                        resolve(data);
                    } else {
                        throw Error(res.statusText);
                    }
                },
                    () => {
                        var data = [{
                            'query': urlQuery,
                            'executed': 'no',
                            'response': res.statusText
                        }]
                        reject(JSON.stringify(data));
                    }
                );
            })
            .catch(function (error) {
                var data = [{
                    'query': urlQuery,
                    'executed': 'no',
                    'response': error
                }]
                reject(JSON.stringify(data));
            });
    });
}